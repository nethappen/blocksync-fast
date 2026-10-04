#!/usr/bin/env python3
"""Benchmark disposable, fully allocated fixtures; never accesses VM images.
Usage: read_performance.py BASELINE [MiB=1024] [repetitions=3]
Each run restores exactly the same target and compatible digest state.
Physical sectors are whole-host diskstats and may contain unrelated traffic.
"""
import json, os, pathlib, shutil, subprocess, sys, tempfile, time
baseline=str(pathlib.Path(sys.argv[1]).resolve())
binary=str(pathlib.Path('src/blocksync-fast').resolve())
size=int(sys.argv[2] if len(sys.argv)>2 else '1024')*1024*1024
repetitions=int(sys.argv[3] if len(sys.argv)>3 else '3')
rows=[]
def disks():
    out={}
    for line in pathlib.Path('/proc/diskstats').read_text().splitlines():
        f=line.split()
        if pathlib.Path('/sys/block',f[2]).exists() and not f[2].startswith(('loop','ram')): out[f[2]]=(int(f[5])*512,int(f[9])*512)
    return out
with tempfile.TemporaryDirectory(prefix='bsf-performance-') as d:
    p=pathlib.Path(d); source=p/'source'; target=p/'target'; digest=p/'digest'; saved=p/'saved-digest'; metrics=p/'metrics'
    chunk=os.urandom(1024*1024)
    with source.open('wb') as f:
        for _ in range(size//len(chunk)): f.write(chunk)
        f.flush(); os.fsync(f.fileno())
    subprocess.run([baseline,'-s',str(source),'-d',str(target),'-f',str(digest)],check=True,stdout=subprocess.DEVNULL)
    shutil.copyfile(digest,saved)
    modes=[('original',baseline,[]),('new-sequential',binary,[])]+[(f'queued-{jobs}',binary,[f'--read-jobs={jobs}','--read-depth=16','--read-size=256K','--direct-read']) for jobs in (1,4,8)]
    for workload in ('unchanged','scattered'):
        for repetition in range(repetitions):
            for name,executable,opts in modes:
                shutil.copyfile(source,target); shutil.copyfile(saved,digest)
                changed=[]
                if workload=='scattered':
                    # Corrupt target and digest together, representing a prior
                    # source version, without changing the stable source.
                    with target.open('r+b') as f, digest.open('r+b') as g:
                        for pos in range(0,size,max(4096,(size//256//4096)*4096)):
                            f.seek(pos); b=f.read(1); f.seek(pos); f.write(bytes([b[0]^255])); changed.append(pos)
                            g.seek(512+(pos//4096)*4); old=g.read(4); g.seek(-4,1); g.write(bytes([old[0]^255])+old[1:])
                with target.open('rb') as f: os.fsync(f.fileno())
                with digest.open('rb') as f: os.fsync(f.fileno())
                # Give buffered runs the same cache state request. Direct runs
                # bypass page cache; no privileged global cache flushing.
                for file in (source,target,digest):
                    with file.open('rb') as f: os.posix_fadvise(f.fileno(),0,0,os.POSIX_FADV_DONTNEED)
                before=disks()
                start=time.monotonic()
                result=subprocess.Popen([executable,'-s',str(source),'-d',str(target),'-f',str(digest),*opts],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                _,status,usage=os.wait4(result.pid,0)
                elapsed=time.monotonic()-start
                result.returncode=os.waitstatus_to_exitcode(status)
                stdout,stderr=result.communicate()
                assert result.returncode==0, stderr
                after=disks(); user,system,rss,inputs,outputs=usage.ru_utime,usage.ru_stime,usage.ru_maxrss,usage.ru_inblock,usage.ru_oublock
                subprocess.run(['cmp','--',str(source),str(target)],check=True)
                row=dict(mode=name,workload=workload,repetition=repetition,size=size,changed_bytes=len(changed)*4096,elapsed_s=elapsed,user_s=user,system_s=system,peak_rss_kib=rss,read_blocks_512=inputs,write_blocks_512=outputs,disk_traffic_bytes={name:[after[name][i]-values[i] for i in (0,1)] for name,values in before.items() if name in after},summary=stdout.decode().splitlines()[-1])
                rows.append(row); print(json.dumps(row),flush=True)
pathlib.Path('tests/performance-results.json').write_text(json.dumps(rows,indent=2)+'\n')
