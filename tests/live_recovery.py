#!/usr/bin/env python3
"""Live content changes converge on the next pass; interrupted digests recover.
Only disposable fixtures are used. Stable bytes are the oracle after convergence.
"""
import os, pathlib, subprocess, tempfile, time
binary=str(pathlib.Path('src/blocksync-fast').resolve())
probe=str(pathlib.Path('.test-build/io-probe.so').resolve())
with tempfile.TemporaryDirectory(prefix='bsf-live-') as d:
    root=pathlib.Path(d); src=root/'source'; dst=root/'target'; dig=root/'digest'; marker=root/'digest.incomplete'
    def command(mode): return [binary,'-s',str(src),'-d',str(dst),'-f',str(dig),*mode]
    def sync(mode, env=None):
        r=subprocess.run(command(mode),capture_output=True,env=env,timeout=30)
        assert not r.returncode,(r.stdout,r.stderr)
        return r
    def same(): subprocess.run(['cmp','--',str(src),str(dst)],check=True)
    for mode in [[],['--mmap'],['--read-jobs=8'],['--read-jobs=8','--direct-read']]:
        for f in root.iterdir(): f.unlink()
        src.write_bytes(b'a'*(8*1024*1024+4097)); sync(mode); same()
        # An old digest which equals the source must not hide stale target bytes
        # after an interrupted write. Recovery must ignore even valid entries.
        with dst.open('r+b') as f: f.seek(0); f.write(b'corruption')
        marker.write_text('interrupted\n')
        before=[(p.read_bytes(),p.stat().st_mtime_ns) for p in [dst,dig,marker]]
        r=sync([*mode,'--dont-write'])
        assert b'Recovering incomplete sync' in r.stdout
        assert before==[(p.read_bytes(),p.stat().st_mtime_ns) for p in [dst,dig,marker]]
        r=sync(mode); same(); assert not marker.exists()
        # Recovery also bypasses partially written digest headers and entries.
        dig.write_bytes(b'partial header'); marker.write_bytes(b'')
        sync(mode); same(); assert not marker.exists()
        if mode==['--mmap']: continue
        e=dict(os.environ,LD_PRELOAD=probe)
        e['BSF_SLOW_AIO' if mode else 'BSF_SLOW_READ']='1' if mode else str(src)
        process=subprocess.Popen(command(mode),env=e,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        deadline=time.monotonic()+10
        while not marker.exists() and time.monotonic()<deadline: time.sleep(.005)
        assert marker.exists()
        # An active sync owns the marker; a second sync must not recover over it.
        other=subprocess.run(command(mode),capture_output=True,timeout=10)
        assert other.returncode and b'cannot lock sync marker' in other.stderr
        with src.open('r+b') as f: f.seek(100); f.write(b'live-change')
        out,err=process.communicate(timeout=30)
        assert not process.returncode,(out,err)
        assert not marker.exists()
        sync(mode); same()
        # Even when a change comes strictly after its block was copied, the
        # next pass detects it through the digest and copies the new bytes.
        with src.open('r+b') as f: f.seek(0); f.write(b'next-pass')
        sync(mode); same()
        print('PASS live content / next-pass convergence / marker locking / recovery',mode,flush=True)
