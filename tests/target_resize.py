#!/usr/bin/env python3
"""Automatic reserved growth, confirmed shrink, unexpected EOF and capacity.
All fixtures are disposable. Successful syncs use cmp as the oracle.
"""
import errno, os, pathlib, subprocess, tempfile, time
binary=str(pathlib.Path('src/blocksync-fast').resolve())
probe=str(pathlib.Path('.test-build/io-probe.so').resolve())
with tempfile.TemporaryDirectory(prefix='bsf-resize-') as d:
    root=pathlib.Path(d); src=root/'source'; dst=root/'target'; dig=root/'digest'; marker=root/'digest.incomplete'; writes=root/'writes'; allocations=root/'allocations'
    modes=[[],['--mmap'],['--read-jobs=8'],['--read-jobs=8','--direct-read']]
    def clear():
        for p in root.iterdir(): p.unlink()
    def env(**extra): return dict(os.environ,LD_PRELOAD=probe,BSF_TARGET=str(dst),BSF_WRITE_LOG=str(writes),BSF_ALLOC_LOG=str(allocations),**extra)
    def command(mode): return [binary,'-s',str(src),'-d',str(dst),'-f',str(dig),*mode]
    def run(mode, success=True, answer='', e=None):
        p=subprocess.run(command(mode),input=answer.encode(),env=e or env(),capture_output=True,timeout=30)
        assert (p.returncode==0)==success,(mode,p.stdout,p.stderr)
        return p
    def same(): subprocess.run(['cmp','--',str(src),str(dst)],check=True)
    def snapshot(p): return (p.read_bytes(),p.stat().st_size,p.stat().st_mtime_ns)
    for mode in modes:
        for allocation_option in [[],['--preallocate']]:
            for old,new,zero in [(0,1,False),(1,4097,False),(4095,4097,False),(4096,262145,True),(262143,262145,False)]:
                clear(); src.write_bytes(b'a'*old); run(mode); same()
                with src.open('ab') as f:f.write((b'\0' if zero else b'b')*(new-old))
                allocations.unlink(missing_ok=True); writes.unlink(missing_ok=True)
                before=[snapshot(p) for p in (dst,dig)]
                run([*mode,*allocation_option,'--dont-write']); assert before==[snapshot(p) for p in (dst,dig)] and not allocations.exists()
                p=run([*mode,*allocation_option]); same(); assert b'warning: source is larger' in p.stderr
                if allocation_option: assert allocations.read_text().splitlines()==[f'1 {old} {new-old}']
                else: assert not allocations.exists()
                assert not marker.exists() and dst.stat().st_size==new
                if zero: assert not writes.exists() or not writes.read_text()
        for reply in ['', 'n\n', 'yes\n', '-y', '--yes']:
            clear(); src.write_bytes(b'a'*4097); run(mode)
            src.write_bytes(b'b')
            before=[snapshot(p) for p in (dst,dig)]
            run([*mode,'--dont-write']); assert before==[snapshot(p) for p in (dst,dig)]
            if reply in ('-y','--yes'): p=run([*mode,reply]); same()
            else:
                p=run(mode,success=reply=='yes\n',answer=reply)
                if reply=='yes\n': same()
                else:
                    assert before==[snapshot(p) for p in (dst,dig)] and not marker.exists()
                    run([*mode,'--force'],success=False)
                    assert before==[snapshot(p) for p in (dst,dig)] and not marker.exists()
            assert b'warning: source is smaller' in p.stderr
        for error in (errno.ENOSPC,errno.EDQUOT,errno.EOPNOTSUPP,errno.EFBIG):
            clear(); src.write_bytes(b'a'*4095); run(mode)
            with src.open('ab') as f:f.write(b'b'*4098)
            before=[snapshot(p) for p in (dst,dig)]
            writes.unlink(missing_ok=True)
            p=run([*mode,'--preallocate'],success=False,e=env(BSF_FAIL_ALLOC=str(dst),BSF_ALLOC_ERRNO=str(error),BSF_FAIL_READ=str(src),BSF_FAIL_AIO='1'))
            assert b'cannot preallocate added target space' in p.stderr
            assert before==[snapshot(p) for p in (dst,dig)] and marker.exists()
            assert not writes.exists()
            run(mode,e=env(BSF_FAIL_ALLOC=str(dst))); same(); assert not marker.exists()
        print('PASS preallocated growth / confirmed shrink / early allocation failures',mode,flush=True)
    for mode in [[],['--read-jobs=8'],['--read-jobs=8','--direct-read']]:
        # Reproduce the zero-tail EOF bug: target shrink must now fail.
        clear()
        with src.open('wb') as f:f.write(b'a'*4096); f.truncate(8*1024*1024)
        dst.write_bytes(b'a'*4096)
        e=env(**({'BSF_SLOW_AIO':'1'} if mode else {'BSF_SLOW_READ':str(src)}))
        process=subprocess.Popen(command(mode),env=e,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        deadline=time.monotonic()+10
        while dst.stat().st_size != src.stat().st_size and process.poll() is None and time.monotonic()<deadline:time.sleep(.005)
        with dst.open('r+b') as f:f.truncate(4096)
        out,err=process.communicate(timeout=30)
        assert process.returncode and marker.exists(),(out,err)
        run(mode); same(); assert not marker.exists()
        # Digest comparison avoids target reads: final size check must catch it.
        e=env(**({'BSF_SLOW_AIO':'1'} if mode else {'BSF_SLOW_READ':str(src)}))
        process=subprocess.Popen(command(mode),env=e,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        while not marker.exists() and process.poll() is None:time.sleep(.005)
        time.sleep(.03)
        with dst.open('r+b') as f:f.truncate(4096)
        out,err=process.communicate(timeout=30)
        assert process.returncode and b'destination size changed' in err and marker.exists(),(out,err)
        run(mode); same()
        # Simulate block-device fstat on a disposable regular fixture.
        clear(); src.write_bytes(b'a'*4096+b'\0'*4096); dst.write_bytes(b'a'*4096)
        before=snapshot(dst)
        p=run([*mode,'--force'],success=False,e=env(BSF_FAKE_BLOCK_PATH=str(dst)))
        assert b'capacity is smaller' in p.stderr and snapshot(dst)==before
        # Different digest paths still contend on the destination inode lock.
        clear(); src.write_bytes(b'a'*(8*1024*1024)); run(mode)
        process=subprocess.Popen(command(mode),env=env(**({'BSF_SLOW_AIO':'1'} if mode else {'BSF_SLOW_READ':str(src)})),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        while not marker.exists() and process.poll() is None:time.sleep(.005)
        time.sleep(.03)
        alternate=root/'other-digest'
        p=subprocess.run([binary,'-s',str(src),'-d',str(dst),'-f',str(alternate),*mode],capture_output=True,timeout=10)
        assert p.returncode and b'destination is locked' in p.stderr,p.stderr
        out,err=process.communicate(timeout=30); assert not process.returncode,(out,err); same()
        print('PASS unexpected destination EOF / final size / small device / target inode lock',mode,flush=True)
