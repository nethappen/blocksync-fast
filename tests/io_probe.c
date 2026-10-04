#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/aio_abi.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
/* Test-only instrumentation: kernel write byte counts, short writes, failures,
 * partial AIO reads and reversed completion batches. */
static int matches(int fd, const char *name) {
    char key[64], path[4096];
    snprintf(key, sizeof(key), "/proc/self/fd/%d", fd);
    ssize_t n = readlink(key, path, sizeof(path)-1);
    if (n < 0) return 0;
    path[n] = 0;
    const char *value = getenv(name);
    return value && !strcmp(path, value);
}
ssize_t write(int fd, const void *buf, size_t n) {
    static ssize_t (*real)(int,const void *,size_t);
    if (!real) real = dlsym(RTLD_NEXT, "write");
    if (fd == STDOUT_FILENO && getenv("BSF_SHORT_STREAM")) {
        static int interrupted;
        if (!interrupted++) { errno = EINTR; return -1; }
        if (n > 17) n = 17;
    }
    return real(fd, buf, n);
}
ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) {
    static ssize_t (*real)(int,const void *,size_t,off_t);
    if (!real) real = dlsym(RTLD_NEXT, "pwrite");
    if (matches(fd, "BSF_SLOW_WRITE")) usleep(200000);
    if (matches(fd, "BSF_FAIL_PATH") && (!getenv("BSF_FAIL_OFFSET") || off >= atoll(getenv("BSF_FAIL_OFFSET")))) { errno = ENOSPC; return -1; }
    static _Thread_local int interrupted_write;
    if (getenv("BSF_EINTR") && !interrupted_write++) { errno = EINTR; return -1; }
    if (getenv("BSF_SHORT_WRITE") && n > 17) n = 17;
    ssize_t result = real(fd, buf, n, off);
    if (result > 0 && matches(fd, "BSF_TARGET")) {
        const char *log = getenv("BSF_WRITE_LOG");
        if (log) {
            int out = open(log, O_WRONLY | O_APPEND | O_CREAT, 0600);
            if (out >= 0) { char line[80]; int len = snprintf(line, sizeof(line), "%lld %zd\n", (long long)off, result); write(out, line, len); close(out); }
        }
    }
    return result;
}
ssize_t pread(int fd, void *buf, size_t n, off_t off) {
    static ssize_t (*real)(int,void *,size_t,off_t);
    if (!real) real = dlsym(RTLD_NEXT, "pread");
    if (matches(fd, "BSF_SLOW_READ")) usleep(200000);
    if (matches(fd, "BSF_FAIL_READ")) { errno = EIO; return -1; }
    static _Thread_local int interrupted_read;
    if (getenv("BSF_EINTR") && !interrupted_read++) { errno = EINTR; return -1; }
    if (getenv("BSF_SHORT_READ") && n > 19) n = 19;
    ssize_t result = real(fd, buf, n, off);
    if (result > 0 && matches(fd, "BSF_SOURCE")) {
        const char *log = getenv("BSF_READ_LOG");
        if (log) {
            int out = open(log, O_WRONLY | O_APPEND | O_CREAT, 0600);
            if (out >= 0) { char line[80]; int len = snprintf(line, sizeof(line), "%lld %zd\n", (long long)off, result); write(out, line, len); close(out); }
        }
    }
    return result;
}
long syscall(long number, ...) {
    static long (*real)(long,...);
    if (!real) real = dlsym(RTLD_NEXT, "syscall");
    va_list ap; va_start(ap, number);
    long result;
    if (number == SYS_gettid) result = real(number);
    else if (number == SYS_io_setup) { unsigned n = va_arg(ap,unsigned); void *c = va_arg(ap,void *); result = real(number,n,c); }
    else if (number == SYS_io_destroy) { aio_context_t c = va_arg(ap,aio_context_t); result = real(number,c); }
    else if (number == SYS_io_submit) {
        aio_context_t c = va_arg(ap,aio_context_t); long n = va_arg(ap,long); struct iocb **cb = va_arg(ap,struct iocb **);
        if (getenv("BSF_SLOW_AIO")) usleep(200000);
        static _Thread_local int interrupted_submit;
        if (getenv("BSF_EINTR") && !interrupted_submit++) { errno = EINTR; result = -1; }
        else if (getenv("BSF_FAIL_AIO")) { errno = EIO; result = -1; }
        else if (getenv("BSF_ZERO_SUBMIT")) result = 0;
        else if (getenv("BSF_AIO_PARTIAL")) { for (long i=0;i<n;i++) if (cb[i]->aio_nbytes > 4096) cb[i]->aio_nbytes = 4096; result = real(number,c,n,cb); }
        else result = real(number,c,n,cb);
    } else if (number == SYS_io_getevents) {
        aio_context_t c = va_arg(ap,aio_context_t); long min = va_arg(ap,long), max = va_arg(ap,long); struct io_event *ev = va_arg(ap,struct io_event *); void *t = va_arg(ap,void *);
        result = real(number,c,min,max,ev,t);
        if (getenv("BSF_FAILED_COMPLETION") && result > 0) ev[0].res = (uint64_t)-EIO;
        if (getenv("BSF_UNSUPPORTED_DIRECT") && result > 0) ev[0].res = (uint64_t)-EINVAL;
        if (getenv("BSF_REVERSE")) for (long i=0;i<result/2;i++) { struct io_event tmp=ev[i]; ev[i]=ev[result-1-i]; ev[result-1-i]=tmp; }
    } else if (number == SYS_io_cancel) { aio_context_t c = va_arg(ap,aio_context_t); void *cb=va_arg(ap,void *), *ev=va_arg(ap,void *); result=real(number,c,cb,ev); }
    else { errno = ENOSYS; result = -1; }
    va_end(ap); return result;
}

int fallocate(int fd, int mode, off_t offset, off_t length) {
    static int (*real)(int,int,off_t,off_t);
    if (!real) real = dlsym(RTLD_NEXT, "fallocate");
    if (matches(fd, "BSF_FAIL_ALLOC")) {
        errno = getenv("BSF_ALLOC_ERRNO") ? atoi(getenv("BSF_ALLOC_ERRNO")) : ENOSPC;
        return -1;
    }
    const char *log = getenv("BSF_ALLOC_LOG");
    if (log) {
        int out = open(log,O_WRONLY|O_APPEND|O_CREAT,0600);
        if (out >= 0) { char line[100]; int n=snprintf(line,sizeof(line),"%d %lld %lld\n",mode,(long long)offset,(long long)length); write(out,line,n); close(out); }
    }
    return real(fd,mode,offset,length);
}
int fstat(int fd, struct stat *st) {
    static int (*real)(int,struct stat *);
    if (!real) real=dlsym(RTLD_NEXT,"fstat");
    int result=real(fd,st);
    if (!result && matches(fd,"BSF_FAKE_BLOCK_PATH")) st->st_mode=(st->st_mode & ~S_IFMT)|S_IFBLK;
    return result;
}
