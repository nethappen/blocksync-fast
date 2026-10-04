#!/usr/bin/env python3
import os, pathlib, signal, subprocess, sys, tempfile, time
binary=str(pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'src/blocksync-fast').resolve())
probe=sys.argv[2] if len(sys.argv)>2 else str(pathlib.Path('.test-build/io-probe.so').resolve())
with tempfile.TemporaryDirectory(prefix='bsf-failures-') as d:
    root=pathlib.Path(d); src=root/'source'; dst=root/'target'; dig=root/'digest'; marker=root/'digest.incomplete'; log=root/'writes'
    def reset():
        for p in root.iterdir(): p.unlink()
        src.write_bytes(b'\xab'*(8*1024*1024+1))
        dst.write_bytes(bytes(src.stat().st_size))
    def cmd(mode): return [binary,'-s',str(src),'-d',str(dst),'-f',str(dig),*mode]
    def checked(mode, env=None, success=True):
        p=subprocess.run(cmd(mode),env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
        assert (p.returncode==0)==success, (p.stdout,p.stderr,p.returncode)
        if success: subprocess.run(['cmp','--',str(src),str(dst)],check=True)
        return p
    for mode in [[],['--read-jobs=8'],['--read-jobs=8','--direct-read']]:
        for sig in [signal.SIGINT,signal.SIGKILL]:
            for phase in ['reads','target','digest']:
                reset()
                e=dict(os.environ,LD_PRELOAD=probe,BSF_TARGET=str(dst),BSF_WRITE_LOG=str(log))
                if phase=='reads':
                    e['BSF_SLOW_AIO' if mode else 'BSF_SLOW_READ']='1' if mode else str(src)
                else: e['BSF_SLOW_WRITE']=str(dst if phase=='target' else dig)
                p=subprocess.Popen(cmd(mode),env=e,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                deadline=time.monotonic()+10
                while not marker.exists() and p.poll() is None and time.monotonic()<deadline: time.sleep(.005)
                assert marker.exists()
                time.sleep(.05)
                assert p.poll() is None
                if mode and phase=='reads':
                    deadline=time.monotonic()+2
                    while p.poll() is None and time.monotonic()<deadline:
                        if len(list(pathlib.Path(f'/proc/{p.pid}/task').iterdir()))==9: break
                        time.sleep(.005)
                    assert len(list(pathlib.Path(f'/proc/{p.pid}/task').iterdir()))==9
                p.send_signal(sig); out,err=p.communicate(timeout=30)
                assert p.returncode!=0 and marker.exists(), (out,err)
                recovered=checked(mode)
                assert b"Recovering incomplete sync" in recovered.stdout and not marker.exists()
        reset()
        e=dict(os.environ,LD_PRELOAD=probe)
        e['BSF_SLOW_AIO' if mode else 'BSF_SLOW_READ']='1' if mode else str(src)
        p=subprocess.Popen(cmd(mode),env=e,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        while not marker.exists() and p.poll() is None: time.sleep(.005)
        time.sleep(.05)
        with src.open('r+b') as f: f.truncate(0)
        out,err=p.communicate(timeout=30)
        assert p.returncode!=0 and marker.exists(), (out,err)
        reset(); checked(mode,env=dict(os.environ,LD_PRELOAD=probe,**({'BSF_FAIL_AIO':'1'} if mode else {'BSF_FAIL_READ':str(src)})),success=False)
        print('PASS interrupts/kill/truncation/read failure',mode,flush=True)
    # No-digest comparisons and permission errors must work on both paths.
    for mode in [[],['--read-jobs=8'],['--read-jobs=8','--direct-read']]:
        reset()
        p=subprocess.run([binary,'-s',str(src),'-d',str(dst),*mode],capture_output=True)
        assert p.returncode==0,p.stderr
        subprocess.run(['cmp','--',str(src),str(dst)],check=True)
        for blocked in [src,dst,dig]:
            if not blocked.exists(): blocked.write_bytes(b'')
            blocked.chmod(0)
            checked(mode,success=False)
            blocked.chmod(0o600)
            marker.unlink(missing_ok=True)
            if blocked==dig: dig.unlink()
    print('PASS no digest and permission failures',flush=True)
    # Check every independent hash context against existing encoding.
    for algo in ['CRC32','MD5','SHA256','XXH32','XXH64','XXH3LOW','XXH3','XXH128']:
        reset(); checked(['-a',algo]); old=dig.read_bytes()[512:]
        dig.unlink(); dst.write_bytes(bytes(src.stat().st_size))
        checked(['--read-jobs=8','-a',algo]); assert dig.read_bytes()[512:]==old
    print('PASS hash encodings',flush=True)
    for i in range(20): checked(['--read-jobs=8','--direct-read'])
    print('PASS repeated queued runs',flush=True)
