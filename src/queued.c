/* Linux native AIO (the kernel interface used by libaio). Each reader owns
 * a contiguous range, an AIO context, reusable slots and private hash state. */
#define _GNU_SOURCE
#include "globals.h"
#include "queued.h"
#include <linux/aio_abi.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <limits.h>

unsigned read_jobs, read_depth = 16;
size_t read_size = 256 * 1024;
int direct_read, read_options, destination_resized, digest_recovery;
static char *marker;
static int marker_active;
static int marker_fd = -1;
static atomic_int interrupted;
static atomic_int failed;
static size_t io_alignment = 4096;
static int source_direct = -1;

size_t read_option_number(const char *s, int units)
{
    char *end;
    unsigned long long n, multiplier = 1;
    errno = 0;
    if (!s || *s < '0' || *s > '9') goto bad;
    n = strtoull(s, &end, 10);
    if (errno || end == s || !n) goto bad;
    if (*end) {
        if (!units) goto bad;
        if (!strcasecmp(end, "K") || !strcasecmp(end, "KiB")) multiplier = 1024;
        else if (!strcasecmp(end, "M") || !strcasecmp(end, "MiB")) multiplier = 1024 * 1024;
        else if (!strcasecmp(end, "G") || !strcasecmp(end, "GiB")) multiplier = 1024ULL * 1024 * 1024;
        else if (!strcasecmp(end, "KB")) multiplier = 1000;
        else if (!strcasecmp(end, "MB")) multiplier = 1000000;
        else if (!strcasecmp(end, "GB")) multiplier = 1000000000;
        else goto bad;
    }
    if (n > SIZE_MAX / multiplier || n * multiplier > LONG_MAX) goto bad;
    return n * multiplier;
bad:
    fprintf(stderr, "%s: invalid positive read option '%s'\n", process_name, s);
    exit(EXIT_FAILURE);
}

void validate_read_options(void)
{
    if (!read_options) return;
    if (!read_jobs) read_jobs = 1;
    if (read_jobs > 64 || read_depth > 256 || read_size > 64 * 1024 * 1024 ||
        read_size < 4096 || read_size % 4096 ||
        read_size > (256ULL * 1024 * 1024) / read_jobs / read_depth) {
        fprintf(stderr, "%s: read limits: jobs 1..64, depth 1..256, size 4K..64M in 4K multiples, total queue <=256MiB\n", process_name);
        exit(EXIT_FAILURE);
    }
    if (flag.oper_mode != BLOCKSYNC || flag.mmap || flag.progress > 1 ||
        !src.path || !dst.path || !strcmp(src.path, "-") || !strcmp(dst.path, "-") ||
        (digest.path && !strcmp(digest.path, "-")) || param.block_size != 4096) {
        fprintf(stderr, "%s: queued reads require block-sync, seekable files/devices and 4K hash blocks; mmap, streams and detailed progress are unsupported\n", process_name);
        exit(EXIT_FAILURE);
    }
}

/* Persist directory entries as well as file contents, including marker removal. */
static int sync_parent(const char *path)
{
    char *copy = strdup(path);
    if (!copy) return -1;
    int fd = open(dirname(copy), O_RDONLY | O_DIRECTORY);
    free(copy);
    if (fd < 0) return -1;
    int result = fsync(fd), saved = errno;
    close(fd);
    errno = saved;
    return result;
}

void sync_guard_check(void)
{
    if (!digest.path) return;
    if (!marker && asprintf(&marker, "%s.incomplete", digest.path) < 0) cleanup(EXIT_FAILURE);
    struct stat st;
    if (!lstat(marker, &st)) {
        fprintf(stderr, "%s: incomplete sync marker '%s': digest cannot be trusted. Remove the digest and marker together, then rerun to compare destination bytes.\n", process_name, marker);
        cleanup(EXIT_FAILURE);
    }
    if (errno != ENOENT) {
        fprintf(stderr, "%s: cannot inspect incomplete marker: %s\n", process_name, strerror(errno));
        cleanup(EXIT_FAILURE);
    }
}

void sync_guard_begin(void)
{
    if (flag.oper_mode != BLOCKSYNC || !digest.path) return;
    if (!marker && asprintf(&marker, "%s.incomplete", digest.path) < 0) cleanup(EXIT_FAILURE);
    int created = 0;
    if (flag.dont_write == 3) {
        marker_fd = open(marker, O_RDONLY | O_NOFOLLOW);
        if (marker_fd < 0 && errno == ENOENT) return;
    } else {
        marker_fd = open(marker, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (marker_fd >= 0) created = 1;
        else if (errno == EEXIST) marker_fd = open(marker, O_RDWR | O_NOFOLLOW);
    }
    if (marker_fd < 0 || flock(marker_fd, LOCK_EX | LOCK_NB)) {
        fprintf(stderr, "%s: cannot lock sync marker '%s' (another sync may be active): %s\n", process_name, marker, strerror(errno));
        cleanup(EXIT_FAILURE);
    }
    struct stat held, named, other;
    if (fstat(marker_fd, &held) || lstat(marker, &named) || !S_ISREG(held.st_mode) ||
        held.st_dev != named.st_dev || held.st_ino != named.st_ino) {
        fprintf(stderr, "%s: sync marker changed while acquiring lock; retry\n", process_name);
        cleanup(EXIT_FAILURE);
    }
    if ((held.st_dev == src.stat.st_dev && held.st_ino == src.stat.st_ino) ||
        (!stat(dst.path, &other) && held.st_dev == other.st_dev && held.st_ino == other.st_ino) ||
        (!stat(digest.path, &other) && held.st_dev == other.st_dev && held.st_ino == other.st_ino)) {
        fprintf(stderr, "%s: sync marker aliases source, destination or digest\n", process_name);
        cleanup(EXIT_FAILURE);
    }
    digest_recovery = !created;
    if (digest_recovery)
        fprintf(flag.prst, "Recovering incomplete sync: comparing destination bytes and rebuilding digest%s\n", flag.dont_write == 3 ? " (dry run)" : "");
    if (flag.dont_write == 3) return;
    const char msg[] = "Incomplete blocksync-fast sync. Next block-sync compares destination bytes.\n";
    if ((created && write_at_all(marker_fd, msg, sizeof(msg)-1, 0) < 0) || fsync(marker_fd) || sync_parent(marker)) {
        fprintf(stderr, "%s: cannot persist incomplete marker: %s\n", process_name, strerror(errno));
        cleanup(EXIT_FAILURE);
    }
    marker_active = 1;
}

void sync_guard_finish(void)
{
    if (flag.oper_mode != BLOCKSYNC || flag.dont_write == 3) return;
    if (dst.fd >= 0 && fsync(dst.fd)) goto bad;
    struct stat target_after;
    if (dst.fd < 0 || fstat(dst.fd, &target_after)) goto bad;
    if (S_ISREG(target_after.st_mode) && (uint64_t)target_after.st_size != src.data_size) {
        fprintf(stderr, "%s: destination size changed during sync; digest remains incomplete\n", process_name);
        cleanup(EXIT_FAILURE);
    }
    if (digest.path && BIT_SET(flag.dont_write, 0)) {
        fprintf(stderr, "%s: destination synced without digest updates; incomplete marker retained for destination comparison on the next sync\n", process_name);
        return;
    }
    if (digest.path && digest.fd >= 0 && fsync(digest.fd)) goto bad;
    /* Persist creation of destination and digest before removing the marker. */
    if (dst.path && sync_parent(dst.path)) goto bad;
    if (digest.path && sync_parent(digest.path)) goto bad;
    if (marker_active && (unlink(marker) || sync_parent(marker))) goto bad;
    marker_active = 0;
    if (marker_fd >= 0) { close(marker_fd); marker_fd = -1; }
    return;
bad:
    fprintf(stderr, "%s: final synchronization failed: %s\n", process_name, strerror(errno));
    cleanup(EXIT_FAILURE);
}

static void stop_signal(int sig) { (void)sig; atomic_store(&interrupted, 1); }
static int stopped(void) { return atomic_load(&interrupted) || atomic_load(&failed); }
static void fail_io(const char *what, off_t offset, int error)
{
    int expected = 0;
    if (atomic_compare_exchange_strong(&failed, &expected, error ? error : EIO))
        fprintf(stderr, "%s: %s at offset %lld: %s\n", process_name, what, (long long)offset, strerror(error ? error : EIO));
}

static int transfer(int fd, void *buf, size_t n, off_t off, int writing)
{
    size_t done = 0;
    while (done < n && !stopped()) {
        ssize_t result = writing ? pwrite(fd, (char *)buf + done, n-done, off+done) :
                                  pread(fd, (char *)buf + done, n-done, off+done);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) { fail_io(writing ? "write" : "read", off+done, errno); return -1; }
        if (!result) {
            fail_io(writing ? "zero-length write" : "unexpected source/digest EOF", off+done, EIO);
            return -1;
        }
        done += result;
    }
    return done == n ? 0 : -1;
}

struct slot { struct iocb cb; char *data; size_t length, done; off_t offset; int busy; };
struct reader {
    pthread_t thread;
    off_t begin, end;
    size_t bytes, blocks;
    pid_t tid;
    aio_context_t context;
    struct slot *slots;
    char *old, *hashes, *previous;
    gcry_md_hd_t hash;
};

static void worker_hash(struct reader *r, void *out, const void *data, size_t length)
{
    if (param.algo.library == LIBGCRYPT) {
        gcry_md_write(r->hash, data, length);
        memcpy(out, gcry_md_read(r->hash, param.algo.value), param.algo.size);
        gcry_md_reset(r->hash);
        return;
    }
#ifdef HAVE_XXHASH
    switch (param.algo.value) {
    case XXHASH_MD_XXH32: XXH32_canonicalFromHash(out, XXH32(data, length, 0)); break;
    case XXHASH_MD_XXH64: XXH64_canonicalFromHash(out, XXH64(data, length, 0)); break;
    case XXHASH_MD_XXH3LOW: XXH32_canonicalFromHash(out, (uint32_t)XXH3_64bits(data, length)); break;
    case XXHASH_MD_XXH3: XXH64_canonicalFromHash(out, XXH3_64bits(data, length)); break;
    case XXHASH_MD_XXH128: XXH128_canonicalFromHash(out, XXH3_128bits(data, length)); break;
    }
#endif
}

static int flush_target(struct reader *r, struct slot *s, size_t begin, size_t end)
{
    if (end == begin) return 0;
    r->bytes += end-begin;
    if (!BIT_SET(flag.dont_write, 1) && transfer(dst.fd, s->data+begin, end-begin, s->offset+begin, 1)) return -1;
    if (flag.write_sync && !BIT_SET(flag.dont_write, 1) && fdatasync(dst.fd)) {
        fail_io("target synchronization", s->offset+begin, errno); return -1;
    }
    return 0;
}

static int process_buffer(struct reader *r, struct slot *s)
{
    size_t count = (s->length + param.block_size-1) / param.block_size;
    size_t hash_bytes = count * param.algo.size;
    off_t digest_offset = HEADER_SIZE + (s->offset / param.block_size) * param.algo.size;
    if (IS_MODE(digest.open_mode, READ)) {
        if (transfer(digest.fd, r->previous, hash_bytes, digest_offset, 0)) return -1;
    } else if (IS_MODE(dst.open_mode, READ)) {
        size_t present = s->length;
        if (BIT_SET(flag.dont_write, 1) && S_ISREG(dst.stat.st_mode)) {
            present = s->offset >= dst.stat.st_size ? 0 : MIN(present, (size_t)(dst.stat.st_size-s->offset));
            memset(r->old, 0, s->length);
        }
        if (transfer(dst.fd, r->old, present, s->offset, 0)) return -1;
    }
    size_t run = 0;
    int all_hashes_match = IS_MODE(digest.open_mode, READ);
    for (size_t i = 0, pos = 0; pos < s->length; i++, pos += param.block_size) {
        if (stopped()) return -1;
        size_t length = MIN(param.block_size, s->length-pos);
        char *hash = r->hashes + i * param.algo.size;
        if (param.hash_use) worker_hash(r, hash, s->data+pos, length);
        int same = IS_MODE(digest.open_mode, READ) ? !memcmp(hash, r->previous+i*param.algo.size, param.algo.size) :
                   IS_MODE(dst.open_mode, READ) && !memcmp(s->data+pos, r->old+pos, length);
        if (same) {
            if (flush_target(r, s, run, pos)) return -1;
            run = pos+length;
        } else { r->blocks++; all_hashes_match = 0; }
    }
    if (flush_target(r, s, run, s->length)) return -1;
    if (IS_MODE(digest.open_mode, WRITE) && !BIT_SET(flag.dont_write, 0) && !all_hashes_match)
        if (transfer(digest.fd, r->hashes, hash_bytes, digest_offset, 1)) return -1;
    return 0;
}

static int submit_slot(struct reader *r, struct slot *s)
{
    memset(&s->cb, 0, sizeof(s->cb));
    s->cb.aio_data = (uint64_t)(uintptr_t)s;
    s->cb.aio_lio_opcode = IOCB_CMD_PREAD;
    s->cb.aio_fildes = direct_read ? source_direct : src.fd;
    s->cb.aio_buf = (uint64_t)(uintptr_t)(s->data+s->done);
    s->cb.aio_nbytes = s->length-s->done;
    s->cb.aio_offset = s->offset+s->done;
    struct iocb *cb = &s->cb;
    /* Single submissions avoid ambiguity after partial batch submissions. */
    while (!stopped()) {
        long result = syscall(SYS_io_submit, r->context, 1L, &cb);
        if (result == 1) { s->busy = 1; return 0; }
        if (result < 0 && errno == EINTR) continue;
        if (result == 0 || (result < 0 && errno == EAGAIN)) return 1;
        fail_io("AIO submission", s->offset+s->done, errno); return -1;
    }
    return -1;
}

static void *read_worker(void *arg)
{
    struct reader *r = arg;
    r->tid = (pid_t)syscall(SYS_gettid);
    if (param.hash_use && param.algo.library == LIBGCRYPT && gcry_md_open(&r->hash, param.algo.value, 0)) {
        fail_io("hash context allocation", r->begin, ENOMEM); return NULL;
    }
    if (syscall(SYS_io_setup, read_depth, &r->context) < 0) { fail_io("AIO queue creation", r->begin, errno); goto out; }
    r->slots = calloc(read_depth, sizeof(*r->slots));
    size_t hashes_size = (read_size / param.block_size) * param.algo.size;
    r->old = malloc(read_size);
    r->hashes = malloc(hashes_size);
    r->previous = malloc(hashes_size);
    if (!r->slots || !r->old || !r->hashes || !r->previous) { fail_io("reader allocation", r->begin, ENOMEM); goto out; }
    for (unsigned i = 0; i < read_depth; i++) {
        if (posix_memalign((void **)&r->slots[i].data, io_alignment, read_size)) { fail_io("aligned reader allocation", r->begin, ENOMEM); goto out; }
    }
    off_t next = r->begin;
    unsigned active = 0;
    while (!stopped()) {
        int blocked = 0;
        for (unsigned i = 0; i < read_depth && !stopped(); i++) {
            struct slot *s = &r->slots[i];
            if (s->busy) continue;
            if (!s->length) {
                if (next >= r->end) continue;
                s->offset = next;
                s->length = MIN(read_size, (size_t)(r->end-next));
                s->done = 0;
                next += s->length;
            }
            /* An unaligned final read uses the buffered descriptor. */
            if (direct_read && s->length % io_alignment) {
                if (transfer(src.fd, s->data, s->length, s->offset, 0) || process_buffer(r, s)) goto out;
                s->length = 0;
                continue;
            }
            int result = submit_slot(r, s);
            if (result < 0) goto out;
            if (result == 1) { blocked = 1; break; }
            active++;
        }
        if (stopped()) break;
        if (!active) {
            if (blocked) { fail_io("AIO submission made no progress", next, EAGAIN); break; }
            if (next >= r->end) break;
            continue;
        }
        struct io_event events[256];
        struct timespec timeout = {0, 100000000};
        long n = syscall(SYS_io_getevents, r->context, 1L, (long)read_depth, events, &timeout);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { fail_io("AIO completion wait", next, errno); break; }
        for (long i = 0; i < n && !stopped(); i++) {
            struct slot *s = (void *)(uintptr_t)events[i].data;
            s->busy = 0;
            active--;
            if (events[i].res2 || (int64_t)events[i].res <= 0 || (uint64_t)events[i].res > s->length-s->done) {
                int error = (int64_t)events[i].res < 0 ? -(int64_t)events[i].res : EIO;
                fail_io("AIO read completion", s->offset+s->done, error); goto out;
            }
            s->done += events[i].res;
            if (s->done < s->length) {
                if (direct_read && s->done % io_alignment) {
                    /* Resume only the unread portion, via buffered I/O. */
                    if (transfer(src.fd, s->data+s->done, s->length-s->done, s->offset+s->done, 0)) goto out;
                    s->done = s->length;
                } else continue;
            }
            if (process_buffer(r, s)) goto out;
            s->length = 0;
        }
    }
out:
    /* io_destroy cancels pending requests and waits for uncancellable reads
     * before any slot memory is released. */
    if (r->context) {
        if (r->slots) for (unsigned i = 0; i < read_depth; i++) if (r->slots[i].busy) {
            struct io_event event;
            syscall(SYS_io_cancel, r->context, &r->slots[i].cb, &event);
        }
        long result;
        do { result = syscall(SYS_io_destroy, r->context); } while (result < 0 && errno == EINTR);
        if (result < 0) fail_io("AIO queue destruction", r->begin, errno);
    }
    if (r->slots) for (unsigned i = 0; i < read_depth; i++) free(r->slots[i].data);
    free(r->slots); free(r->old); free(r->hashes); free(r->previous);
    if (r->hash) gcry_md_close(r->hash);
    return NULL;
}

int queued_sync(void)
{
    if (param.block_size != 4096) { fprintf(stderr, "%s: queued reads require a 4K digest block size\n", process_name); return -1; }
    if (direct_read) {
        source_direct = open(src.path, O_RDONLY | O_DIRECT);
        if (source_direct < 0) { fprintf(stderr, "%s: direct reads unsupported or source open failed: %s (no fallback)\n", process_name, strerror(errno)); return -1; }
        struct stat st;
        if (fstat(source_direct, &st) || st.st_dev != src.stat.st_dev || st.st_ino != src.stat.st_ino || st.st_rdev != src.stat.st_rdev) {
            fprintf(stderr, "%s: source changed while opening direct descriptor\n", process_name); close(source_direct); return -1;
        }
#ifdef STATX_DIOALIGN
        struct statx sx;
        if (!statx(source_direct, "", AT_EMPTY_PATH, STATX_DIOALIGN, &sx) && (sx.stx_mask & STATX_DIOALIGN) && sx.stx_dio_mem_align && sx.stx_dio_offset_align) {
            io_alignment = MAX(sx.stx_dio_mem_align, sx.stx_dio_offset_align);
        } else
#endif
        if (S_ISBLK(src.stat.st_mode)) {
            unsigned sector;
            if (ioctl(source_direct, BLKSSZGET, &sector) < 0) { close(source_direct); return -1; }
            io_alignment = MAX(4096, sector);
        }
        if (io_alignment < sizeof(void *)) io_alignment = sizeof(void *);
        if ((io_alignment & (io_alignment-1)) || io_alignment > read_size || read_size % io_alignment) {
            fprintf(stderr, "%s: read size does not satisfy direct I/O alignment %zu\n", process_name, io_alignment); close(source_direct); return -1;
        }
    }
    /* Range ownership is rounded to both hash and direct alignment units. */
    size_t unit = MAX(param.block_size, io_alignment);
    size_t units = src.data_size / unit + (src.data_size % unit != 0);
    struct reader *readers = calloc(read_jobs, sizeof(*readers));
    if (!readers) { if (source_direct >= 0) close(source_direct); return -1; }
    struct sigaction handler = {0}, old_int, old_term;
    handler.sa_handler = stop_signal;
    sigemptyset(&handler.sa_mask);
    sigaction(SIGINT, &handler, &old_int); sigaction(SIGTERM, &handler, &old_term);
    unsigned started = 0;
    fprintf(flag.prst, "Queued source reads: %u jobs, depth %u, %zu bytes/read, %zu bytes outstanding%s\n", read_jobs, read_depth, read_size, read_jobs * read_depth * read_size, direct_read ? ", direct" : "");
    for (unsigned i = 0; i < read_jobs; i++) {
        size_t first = (units / read_jobs) * i + MIN(i, units % read_jobs);
        size_t last = first + units / read_jobs + (i < units % read_jobs);
        readers[i].begin = MIN(first * unit, src.data_size);
        readers[i].end = MIN(last * unit, src.data_size);
        int error = pthread_create(&readers[i].thread, NULL, read_worker, &readers[i]);
        if (error) { fail_io("reader thread creation", readers[i].begin, error); break; }
        started++;
    }
    for (unsigned i = 0; i < started; i++) {
        pthread_join(readers[i].thread, NULL);
        prog.wri_bytes += readers[i].bytes;
        prog.wri_blocks += readers[i].blocks;
    }
    sigaction(SIGINT, &old_int, NULL); sigaction(SIGTERM, &old_term, NULL);
    free(readers);
    if (source_direct >= 0) close(source_direct);
    if (atomic_load(&interrupted)) fprintf(stderr, "%s: sync interrupted; digest remains incomplete\n", process_name);
    if (stopped()) return -1;
    struct stat after;
    if (fstat(src.fd, &after) || (S_ISREG(after.st_mode) && (after.st_size != src.stat.st_size))) {
        fprintf(stderr, "%s: source size changed during sync\n", process_name); return -1;
    }
    return 0;
}
