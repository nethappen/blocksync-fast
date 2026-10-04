#!/usr/bin/env python3
"""Disposable fixtures; cmp is the oracle. Build io_probe.c for write counts/faults.
Run: python3 tests/queued_reads.py [binary] [.test-build/io-probe.so] [baseline-binary]
"""
import os, pathlib, subprocess, sys, tempfile, struct
BIN = str(pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'src/blocksync-fast').resolve())
PROBE = sys.argv[2] if len(sys.argv)>2 else str(pathlib.Path('.test-build/io-probe.so').resolve())
BASE = sys.argv[3] if len(sys.argv)>3 else None
calls = 0
with tempfile.TemporaryDirectory(prefix='bsf-tests-') as directory:
    root = pathlib.Path(directory)
    source, target, digest, log = [root/n for n in ('source','target','digest','writes')]
    def reset(n, sparse=False):
        for p in root.iterdir(): p.unlink()
        if sparse:
            with source.open('wb') as f: f.truncate(n)
        else: source.write_bytes((bytes(range(256))*((n+255)//256))[:n])
    def run(opts=(), ok=True, env=None, binary=BIN):
        global calls
        calls += 1
        e = dict(os.environ, LD_PRELOAD=PROBE, BSF_TARGET=str(target), BSF_WRITE_LOG=str(log))
        if env: e.update(env)
        p = subprocess.run([binary,'-s',str(source),'-d',str(target),'-f',str(digest),*opts],env=e,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
        assert b'cannot be preloaded' not in p.stderr, p.stderr
        assert (p.returncode == 0) == ok, (opts, p.returncode, p.stdout, p.stderr)
        return p
    def cmp(): subprocess.run(['cmp','--',str(source),str(target)],check=True)
    def written():
        return sum(int(line.split()[1]) for line in log.read_text().splitlines()) if log.exists() else 0
    def forget_log():
        if log.exists(): log.unlink()
    def snapshot(p):
        return (p.read_bytes(),p.stat().st_size,p.stat().st_mtime_ns) if p.exists() else None
    modes = [[],['--mmap'],['--read-jobs=1','--read-depth=1'],['--read-jobs=2','--read-depth=2'],['--read-jobs=4'],['--read-jobs=8'],['--read-jobs=8','--direct-read']]
    sizes = [0,1,4095,4096,4097,262143,262144,262145,32767,32768,32769,64*1024*1024+4097]
    for mode in modes:
        for n in sizes:
            reset(n); run(mode); cmp()
            expected = digest.read_bytes()[512:]
            old_mtime=target.stat().st_mtime_ns
            forget_log(); run(mode); cmp(); assert written()==0, (mode,n,written())
            assert target.stat().st_mtime_ns==old_mtime
            if n:
                before_source=source.read_bytes()
                changes = sorted({0,n-1,*[p for p in [4095,4096,8192,262143,262144,n//8,n//4,n//2] if p<n]})
                units=(n+4095)//4096
                jobs=next((int(arg.split('=')[1]) for arg in mode if arg.startswith('--read-jobs=')),1)
                for worker in range(1,jobs):
                    boundary=((units//jobs)*worker+min(worker,units%jobs))*4096
                    changes.extend(pos for pos in [boundary-1,boundary,boundary+1] if 0<=pos<n)
                with source.open('r+b') as f:
                    for p in changes: f.seek(p); f.write(b'\x7f')
                forget_log(); run(mode); cmp()
                after_source=source.read_bytes()
                expected_bytes=sum(min(4096,n-pos) for pos in range(0,n,4096) if before_source[pos:pos+4096]!=after_source[pos:pos+4096])
                if '--mmap' not in mode: assert written()==expected_bytes, (mode,n,written(),expected_bytes)
            # independent legacy digest entry comparison
            other = root/'digest-other'
            if n:
                subprocess.run([BIN,'-s',str(source),'-f',str(other),'--make-digest'],check=True,stdout=subprocess.DEVNULL)
                assert digest.read_bytes()[512:]==other.read_bytes()[512:], (mode,n,'digest mismatch')
                other.unlink()
            a,b = snapshot(target),snapshot(digest)
            run([*mode,'--dont-write']); assert (a,b)==(snapshot(target),snapshot(digest))
            digest.unlink(); forget_log(); run(mode); cmp(); assert written()==0
        print('PASS sizes/copy/changes/digest/dry-run/missing-digest',mode,flush=True)
    for mode in modes:
        for n in [4097,262145,2*1024*1024+1]:
            reset(n, sparse=True)
            # Holes begin/end inside hash blocks, plus allocated zero extents.
            with source.open('r+b') as f:
                f.seek(n//3); f.write(b'nonzero')
                f.seek(n//2); f.write(bytes(min(4096,n-n//2)))
            target.write_bytes(b'\xff'*n)
            run(mode); cmp()
            # Forced sizing and non-mutating dry-run on size mismatch.
            digest.unlink(); target.write_bytes(b'x'*(n+4096))
            a,b=snapshot(target),snapshot(digest)
            run([*mode,'--force','--dont-write']); assert (a,b)==(snapshot(target),snapshot(digest))
            run([*mode,'-y']); cmp()
            digest.unlink(); target.write_bytes(b'x')
            run([*mode,'-y']); cmp()
    print('PASS holes/zero extents/stale destination/sizing',flush=True)
    for jobs in [1,2,4,8]:
        for depth in [1,2,16]:
            reset(262145); run([f'--read-jobs={jobs}',f'--read-depth={depth}']); cmp()
    reset(4097)
    for option in ['--read-jobs=0','--read-jobs=-1','--read-jobs=65','--read-jobs=4294967297','--read-depth=0','--read-depth=257','--read-size=256KiBX','--read-size=18446744073709551616K','--read-size=0','--read-size=4097','--read-size=64M']:
        run(['--read-jobs=8',option],ok=False)
    for extra in [['--mmap'],['--make-delta'],['--make-digest'],['--progress-detail'],['-b','8K']]: run(['--direct-read',*extra],ok=False)
    for mode in [[],['--read-jobs=8']]:
        reset(4097); os.link(source,target); run(mode,ok=False); assert snapshot(source)==snapshot(target)
        target.unlink(); target.symlink_to(source); run(mode,ok=False)
        reset(4097); run([*mode,'--dont-write']); assert not target.exists() and not digest.exists()
        reset(4097); run(mode); cmp()
        # Every malformed digest should fail before a success claim.
        original=digest.read_bytes()
        for malformed in [b'',original[:511],original[:-1],b'bad'+original[3:],original[:24]+struct.pack('Q',100)+original[32:],original[:32]+struct.pack('Q',8192)+original[40:],original[:56]+struct.pack('Q',9999)+original[64:]]:
            digest.write_bytes(malformed); run([*mode,'-b','4K','-a','XXH3LOW'],ok=False)
            marker=root/'digest.incomplete'
            if marker.exists(): marker.unlink()
        digest.write_bytes(original)
        # Partial writes and interrupted operations must be retried.
        source.write_bytes(b'y'*4097); run(mode,env={'BSF_SHORT_WRITE':'1','BSF_SHORT_READ':'1','BSF_EINTR':'1'}); cmp()
        for fail_path in [target,digest]:
            source.write_bytes(bytes([fail_path==target])*4097)
            run(mode,ok=False,env={'BSF_FAIL_PATH':str(fail_path),'BSF_FAIL_OFFSET':str(512 if fail_path==digest else 0)})
            assert (root/'digest.incomplete').exists()
            run(mode); cmp(); assert not (root/'digest.incomplete').exists()
    for direct in [False,True]:
        mode=['--read-jobs=8']+(['--direct-read'] if direct else [])
        reset(2*1024*1024+4097)
        run(mode,env={'BSF_AIO_PARTIAL':'1','BSF_REVERSE':'1','BSF_EINTR':'1'}); cmp()
        for fault in ['BSF_FAIL_AIO','BSF_ZERO_SUBMIT','BSF_FAILED_COMPLETION','BSF_UNSUPPORTED_DIRECT']:
            source.write_bytes(b'x'*source.stat().st_size)
            run(mode,ok=False,env={fault:'1'})
            assert (root/'digest.incomplete').exists()
            run(mode); cmp(); assert not (root/'digest.incomplete').exists()
    if BASE:
        for mode in [[],['--read-jobs=8','--direct-read']]:
            reset(262145); run(binary=BASE); cmp(); before=digest.read_bytes()[512:]
            forget_log(); run(mode); cmp(); assert written()==0 and before==digest.read_bytes()[512:]
        print('PASS original binary digest compatibility',flush=True)
    print(f'PASS {calls} sync invocations',flush=True)
