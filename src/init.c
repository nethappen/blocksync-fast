#define _GNU_SOURCE
/*
 ./src/init.c - this file is a part of program blocksync-fast

 Copyright (C) 2024 Marcin Koczwara <mk@nethorizon.pl>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#include "globals.h"
#include "queued.h"
#include "era.h"
#include <linux/fs.h>
#include <linux/falloc.h>
#include <sys/file.h>

void init_map_methods(void)
{
    if (flag.mmap == 1)
    {
        fprintf(flag.prst, "Uses mmap as an alternative read and write method\n");
        src.open_mode |= MMAP;
        dst.open_mode |= MMAP;
        digest.open_mode |= MMAP;
        delta.open_mode |= MMAP;
    }
    else
    {
        src.open_mode |= DIRECT;
        dst.open_mode |= DIRECT;
        digest.open_mode |= DIRECT;
        delta.open_mode |= DIRECT;
    }

    if (flag.write_sync == 1)
        fprintf(flag.prst, "Syncs and flushes data to a disk device defined by the buffer size in bytes\n");
}

void check_algo_param(void)
{
    for (int i = 0; i <= 1024; i++)
    {
        if (algos[i].value == 0)
        {
            fprintf(stderr, "%s: invalid hash algorithm '%s'\n",
                    process_name, param.hash_algo);

            fprintf(flag.prst, "Try '%s --list-algos' for more information.\n",
                    process_name);

            cleanup(EXIT_FAILURE);
        }

        if (strcasecmp(param.hash_algo, algos[i].symbol) == 0)
        {
            param.algo = algos[i];
            break;
        }
    }
}

void check_block_size(void)
{
    if (param.block_size < 1)
    {
        fprintf(stderr, "%s: the block size has 0 bytes\n", process_name);
        cleanup(EXIT_FAILURE);
    }
}

void init_src_device(void)
{
    if (strcmp(src.path, "-") == 0) {

        if (param.h_data_size == NULL) {
            fprintf(stderr, "%s: missing required data size data size parameter for stdin\n", process_name);
            fprintf(flag.prst, "Please add '-S or --size=N[KMG]' to specify data size\n");
            cleanup(EXIT_FAILURE);
        }

        src.fd = STDIN_FILENO;
        src.open_mode = PIPE_R;
        src.data_size = param.data_size;

        fprintf(flag.prst, "Source device: STDIN has specified size of %s\n",
            format_units(src.data_size, true));
    }
    else {

        src.fd = open(src.path, O_RDONLY);

        if (src.fd < 0 || fstat(src.fd, &src.stat) < 0)
        {
            fprintf(stderr, "%s: unable to open source file or device \'%s\': %s\n",
                    process_name, src.path, strerror(errno));
            cleanup(EXIT_FAILURE);
        }

        src.open_mode |= READ;
        off_t source_size = lseek(src.fd, 0, SEEK_END);
        if (source_size < 0 || lseek(src.fd, 0, SEEK_SET) < 0) {
            fprintf(stderr, "%s: source must be seekable: %s\n", process_name, strerror(errno));
            cleanup(EXIT_FAILURE);
        }
        src.data_size = source_size;

        if (param.h_data_size != NULL && param.data_size != src.data_size) {
            if (param.data_size > src.data_size) {
                fprintf(stderr, "%s: specified data size '%s' is greater than '%s'\n",
                    process_name, format_units(param.data_size, true), format_units(src.data_size, true));
                cleanup(EXIT_FAILURE);
            }
            
            src.data_size = param.data_size;
            fprintf(flag.prst, "Source device: '%s' has override size of %s\n",
                    src.path, format_units(src.data_size, true));
        }
        else
            fprintf(flag.prst, "Source device: '%s' has size of %s\n",
                    src.path, format_units(src.data_size, true));
    }

    if (src.data_size < 1 && flag.oper_mode != BLOCKSYNC)
    {
        fprintf(stderr, "%s: source device is empty\n", process_name);
        cleanup(EXIT_FAILURE);
    }

    param.num_blocks = (src.data_size / param.block_size) + (src.data_size % param.block_size > 0 ? 1 : 0);
    param.data_size = src.data_size;
    era_check_source();
}

int assume_yes, preallocate;
static struct stat shrink_approved;
static int shrink_confirmed;

/* Ask before creating a marker or mutating destination/digest metadata. */
void confirm_target_shrink(void)
{
    struct stat st;
    if (stat(dst.path, &st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size <= src.data_size)
        return;
    fprintf(stderr, "%s: warning: source is smaller; target '%s' would shrink from %s to %s\n",
            process_name, dst.path, format_units(st.st_size, true), format_units(src.data_size, true));
    if (BIT_SET(flag.dont_write, 1)) return;
    if (!assume_yes) {
        char answer[32];
        if (!strcmp(src.path, "-")) {
            fprintf(stderr, "%s: shrinking with stdin as source requires -y\n", process_name);
            cleanup(EXIT_FAILURE);
        }
        fprintf(stderr, "Shrink target and discard trailing bytes? [y/N] ");
        fflush(stderr);
        if (!fgets(answer, sizeof(answer), stdin)) answer[0] = 0;
        answer[strcspn(answer, "\r\n")] = 0;
        if (strcasecmp(answer, "y") && strcasecmp(answer, "yes")) {
            fprintf(stderr, "%s: target shrink declined; no target or digest changes\n", process_name);
            cleanup(EXIT_FAILURE);
        }
    }
    shrink_approved = st;
    shrink_confirmed = 1;
}

static void resize_sync_target(size_t old_size)
{
    if (src.data_size > old_size) {
        fprintf(stderr, "%s: warning: source is larger; growing target '%s' from %s to %s\n",
                process_name, dst.path, format_units(old_size, true), format_units(src.data_size, true));
        if (preallocate && !BIT_SET(flag.dont_write, 1)) {
            /* Reserve before changing EOF. Never use an emulated zero-write
             * fallback: unsupported allocation must fail before copying. */
            int result;
            do { result = fallocate(dst.fd, FALLOC_FL_KEEP_SIZE, old_size, src.data_size-old_size); }
            while (result < 0 && errno == EINTR);
            if (result < 0) {
                fprintf(stderr, "%s: cannot preallocate added target space (no data copied): %s\n", process_name, strerror(errno));
                cleanup(EXIT_FAILURE);
            }
        }
    } else if (src.data_size < old_size && !BIT_SET(flag.dont_write, 1) && !assume_yes &&
               (!shrink_confirmed || shrink_approved.st_dev != dst.stat.st_dev ||
                shrink_approved.st_ino != dst.stat.st_ino || (size_t)shrink_approved.st_size != old_size)) {
        fprintf(stderr, "%s: target changed since shrink confirmation; retry or use -y\n", process_name);
        cleanup(EXIT_FAILURE);
    }
    destination_resized = 1;
    dst.data_size = src.data_size;
    dev_truncate(&dst);
}

void init_dst_device(void)
{
    if (access(dst.path, F_OK) == 0)
        dst.open_mode |= READ;

    dst.fd = open(dst.path, BIT_SET(flag.dont_write, 1) ? O_RDONLY : O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (dst.fd < 0 && BIT_SET(flag.dont_write, 1) && errno == ENOENT) {
        dst.data_size = src.data_size;
        dst.open_mode &= ~READ;
        dst.open_mode |= WRITE;
        return;
    }

    if (dst.fd < 0 || fstat(dst.fd, &dst.stat) < 0)
    {
        fprintf(stderr, "%s: unable to %s target device \'%s\': %s\n",
                process_name, IS_MODE(dst.open_mode, READ) ? "open" : "create",
                dst.path, strerror(errno));

        cleanup(EXIT_FAILURE);
    }

    if (flag.oper_mode == BLOCKSYNC &&
        ((src.stat.st_dev == dst.stat.st_dev && src.stat.st_ino == dst.stat.st_ino) ||
         (S_ISBLK(src.stat.st_mode) && S_ISBLK(dst.stat.st_mode) && src.stat.st_rdev == dst.stat.st_rdev))) {
        fprintf(stderr, "%s: source and destination alias\n", process_name);
        cleanup(EXIT_FAILURE);
    }

    if (flag.oper_mode == BLOCKSYNC && flock(dst.fd, LOCK_EX | LOCK_NB)) {
        fprintf(stderr, "%s: destination is locked by another sync: %s\n", process_name, strerror(errno));
        cleanup(EXIT_FAILURE);
    }

    if (!IS_MODE(dst.open_mode, READ))
    {
        destination_resized = 1;
        fprintf(flag.prst, "Creating target device: '%s'\n",
                dst.path);

        if (flag.oper_mode == BLOCKSYNC && S_ISREG(dst.stat.st_mode))
            resize_sync_target(dst.stat.st_size);
        else {
            if (flag.oper_mode == BLOCKSYNC) dst.data_size = src.data_size;
            else if (flag.oper_mode == APPLYDELTA) dst.data_size = delta_header.data_size;
            dev_truncate(&dst);
        }
    }
    else
    {
        dst.data_size = lseek(dst.fd, 0, SEEK_END);
        lseek(dst.fd, 0, SEEK_SET);

        fprintf(flag.prst, "Target device: '%s' has size of %s\n",
                dst.path, format_units(dst.data_size, true));

        if (flag.oper_mode == BLOCKSYNC && dst.data_size != src.data_size) {
            if (S_ISREG(dst.stat.st_mode)) resize_sync_target(dst.data_size);
            else {
                if (dst.data_size < src.data_size) {
                    fprintf(stderr, "%s: destination device capacity is smaller than source; --force cannot enlarge it\n", process_name);
                    cleanup(EXIT_FAILURE);
                }
                if (!flag.force) {
                    fprintf(stderr, "%s: destination device is larger; use --force to sync only the source-sized prefix\n", process_name);
                    cleanup(EXIT_FAILURE);
                }
                destination_resized = 1;
                dst.data_size = src.data_size;
            }
        }

        if (flag.oper_mode == APPLYDELTA && dst.data_size != delta_header.data_size)
        {
            fprintf(stderr, "%s: size of target device mismatch.\n", process_name);

            if (flag.force == 0)
            {
                fprintf(flag.prst, "Try add '--force' argument \n");
                cleanup(EXIT_FAILURE);
            }
            fprintf(flag.prst, "Applying new size to target device: %s\n", format_units(delta_header.data_size, true));

            dst.data_size = delta_header.data_size;
            dev_truncate(&dst);
        }
    }

    dst.open_mode |= WRITE;

    if (flag.no_compare == 1)
        dst.open_mode &= ~READ;
}

void init_digest_file()
{
    if (digest.path != NULL) {
        if (access(digest.path, F_OK) == 0)
            digest.open_mode |= READ;

        if (flag.oper_mode != BLOCKSYNC) sync_guard_check();
        digest.fd = open(digest.path, BIT_SET(flag.dont_write, 0) ? O_RDONLY : O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        if (digest.fd < 0 && BIT_SET(flag.dont_write, 0) && errno == ENOENT) {
            digest.open_mode &= ~READ;
            digest.fd = -1;
            /* Virtual missing digest for dry runs: no file is created. */
        } else

        if (digest.fd < 0 || fstat(digest.fd, &digest.stat) < 0)
        {
            fprintf(stderr, "%s: unable to %s digest file \'%s\': %s\n",
                    process_name, IS_MODE(digest.open_mode, READ) ? "open" : "create",
                    digest.path, strerror(errno));

            cleanup(EXIT_FAILURE);
        }
    }
    else {
        fprintf(flag.prst, "Data to make-digest is write to STDOUT\n");
        digest.fd = STDOUT_FILENO;
        digest.open_mode = PIPE;
    }

    /* After interruption, even a valid-looking digest may describe writes
     * that never reached the destination. Ignore its header and entries. */
    if (flag.oper_mode == BLOCKSYNC && digest_recovery)
        digest.open_mode &= ~READ;
    digest.open_mode |= WRITE;
    param.hash_use = true;

    if (digest.path != NULL) {
        if (!IS_MODE(digest.open_mode, READ))
            fprintf(flag.prst, "Creating digest file: '%s'\n",
                    digest.path);
        else
        {
            digest.data_size = lseek(digest.fd, 0, SEEK_END);
            lseek(digest.fd, 0, SEEK_SET);

            fprintf(flag.prst, "Digest file: '%s' has size of %s\n",
                    digest.path, format_units(digest.data_size, true));
        }

        if (IS_MODE(digest.open_mode, READ) && digest.data_size < HEADER_SIZE)
        {
            fprintf(stderr, "%s: digest file '%s' is invalid\n", process_name, digest.path);
            if (flag.force == 0)
            {
                fprintf(flag.prst, "Try add '--force' argument to overwrite digest file\n");
                cleanup(EXIT_FAILURE);
            }

            digest.open_mode &= ~READ;
            digest.data_size = HEADER_SIZE;
        }
    }

    if (IS_MODE(digest.open_mode, MMAP))
        digest.max_buf_size = ((int)HEADER_SIZE < PAGE_SIZE ? PAGE_SIZE : (size_t)HEADER_SIZE);

    if (IS_MODE(digest.open_mode, DIRECT) || IS_MODE(digest.open_mode, PIPE))
    {
        digest.max_buf_size = (size_t)HEADER_SIZE;
        digest.buf_data = malloc(digest.max_buf_size);
    }

    if (IS_MODE(digest.open_mode, READ))
        map_buffer(&digest);

    if (IS_MODE(digest.open_mode, READ))
    {
        digest_read_header();
        if (memcmp(digest_header.recognize, (const void *)(MAGIC_DIGEST), sizeof(MAGIC_DIGEST)) != 0 || digest_header.block_size < 1)
        {
            fprintf(stderr, "%s: digest file '%s' is invalid\n", process_name, digest.path);
            if (flag.force == 0)
            {
                fprintf(flag.prst, "Try add '--force' argument to overwrite digest file\n");
                cleanup(EXIT_FAILURE);
            }
            fprintf(flag.prst, "Warning: overwriting digest file\n");
            digest.open_mode &= ~READ;
        }
    }

    if (IS_MODE(digest.open_mode, READ))
    {
        if (digest_header.data_size != src.data_size)
        {
            fprintf(flag.prst, "Digest covers a different source size; comparing destination bytes and rebuilding entries\n");
        }

        if (digest_header.block_size != param.block_size)
        {
            if (param.h_block_size == NULL)
            {
                param.block_size = digest_header.block_size;
                param.num_blocks = (src.data_size / param.block_size) + (src.data_size % param.block_size > 0 ? 1 : 0);
            }
            else
            {
                fprintf(stderr, "%s: digest was written for a block size of %s\n", process_name, format_units(digest_header.block_size, true));
                if (flag.force == 0)
                {
                    fprintf(flag.prst, "Try add '--force' argument to overwrite digest file\n");
                    cleanup(EXIT_FAILURE);
                }
                fprintf(flag.prst, "Warning: overwriting digest file\n");
                digest.open_mode &= ~READ;
            }
        }

        if (digest_header.hash_type != param.algo.value)
        {
            struct symbol_value_desc _algo;

            for (int i = 0; i <= 1024; i++)
            {
                _algo = algos[i];

                if (_algo.value == 0)
                {
                    fprintf(stderr, "%s: digest file has unsupported hash algorithm\n", process_name);
                    if (flag.force == 0)
                    {
                        fprintf(flag.prst, "Try add '--force' argument to overwrite digest file\n");
                        cleanup(EXIT_FAILURE);
                    }
                    fprintf(flag.prst, "Warning: overwriting digest file\n");
                    digest.open_mode &= ~READ;

                    _algo = param.algo;
                    break;
                }

                if (digest_header.hash_type == _algo.value)
                    break;
            }

            if (param.hash_algo == NULL)
            {
                param.algo = _algo;
            }
            else
            {
                fprintf(stderr, "%s: '%s' was written for a different hash algo '%s'\n", process_name, digest.path, _algo.symbol);
                if (flag.force == 0)
                {
                    fprintf(flag.prst, "Try add '--force' argument to overwrite digest file\n");
                    cleanup(EXIT_FAILURE);
                }
                fprintf(flag.prst, "Warning: overwriting digest file\n");
                digest.open_mode &= ~READ;
            }
        }

        digest.rel_off = 0;
    }

    if (IS_MODE(digest.open_mode, READ) &&
        (digest_header.total_blocks != (digest_header.data_size / digest_header.block_size + (digest_header.data_size % digest_header.block_size != 0)) ||
         digest_header.total_blocks > (SIZE_MAX - HEADER_SIZE) / param.algo.size ||
         digest.data_size != HEADER_SIZE + digest_header.total_blocks * param.algo.size)) {
        fprintf(stderr, "%s: malformed or truncated digest body\n", process_name);
        if (!flag.force) cleanup(EXIT_FAILURE);
        digest.open_mode &= ~READ;
    }
    if (IS_MODE(digest.open_mode, READ) && digest_header.data_size != src.data_size)
        digest.open_mode &= ~READ;
    /* A digest cannot stand in for destination bytes after target sizing or
     * creation. Recompare destination and rebuild entries in that case. */
    if (flag.oper_mode == BLOCKSYNC &&
        (destination_resized || !IS_MODE(dst.open_mode, READ)))
        digest.open_mode &= ~READ;
    if (read_options && param.block_size != 4096) {
        fprintf(stderr, "%s: queued reads require a 4K digest block size\n", process_name);
        cleanup(EXIT_FAILURE);
    }
    era_require_digest();
    digest.data_size = HEADER_SIZE + (param.num_blocks * param.algo.size);
    dev_truncate(&digest);

    strcpy(digest_header.recognize, MAGIC_DIGEST);
    strcpy(digest_header.version, BSF_VERSION);
    digest_header.data_size = src.data_size;
    digest_header.block_size = param.block_size;
    digest_header.total_blocks = param.num_blocks;
    digest_header.timestamp = time(NULL);
    digest_header.hash_type = param.algo.value;
    memset(digest_header.padding, '\0', sizeof(digest_header.padding));

    if (!BIT_SET(flag.dont_write, 0) && IS_MODE(digest.open_mode, MMAP))
    {
        if (digest.buf_data == NULL)
            map_buffer(&digest);

        get_ptr(&digest);
        memcpy((void *)digest.ptr_w, (const void *)&digest_header, (size_t)sizeof(digest_header));
    }

    if (!BIT_SET(flag.dont_write, 0) && IS_MODE(digest.open_mode, DIRECT))
        if (write_at_all(digest.fd, (const void *)&digest_header, (size_t)sizeof(digest_header), (off_t)0) < 0)
        {
            fprintf(stderr, "%s: error while writing to '%s' : %s\n", process_name, digest.path, strerror(errno));
            cleanup(EXIT_FAILURE);
        }

    if (!BIT_SET(flag.dont_write, 0) && IS_MODE(digest.open_mode, PIPE) && !isatty(STDOUT_FILENO))
    {
        if (write(digest.fd, (const void *)&digest_header, (size_t)sizeof(digest_header)) < 0)
        {
            fprintf(stderr, "%s: error while writing to stdout: %s\n", process_name, strerror(errno));
            cleanup(EXIT_FAILURE);
        }
    }

    digest.abs_off = digest.rel_off = HEADER_SIZE;
    sync_data(&digest);

    if (flag.no_compare == 1)
        digest.open_mode &= ~READ;
}

void init_dst_delta(void)
{
    bool file_exist = false;

    if (delta.path != NULL)
    {
        if (access(delta.path, F_OK) == 0)
        {
            fprintf(stderr, "%s: file exists '%s'\n", process_name, delta.path);

            if (flag.force == 0)
            {
                fprintf(flag.prst, "Try add '--force' argument \n");
                cleanup(EXIT_FAILURE);
            }
        }

        fprintf(flag.prst, "%s target delta file: '%s'\n", (file_exist ? "Overwriting" : "Creating"), delta.path);
        delta.fd = open(delta.path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

        if (delta.fd < 0 || fstat(delta.fd, &delta.stat) < 0)
        {
            fprintf(stderr, "%s: unable to open delta file \'%s\': %s\n",
                    process_name, delta.path, strerror(errno));

            cleanup(EXIT_FAILURE);
        }
    }
    else
    {
        fprintf(flag.prst, "Data to make-delta is write to STDOUT\n");
        delta.fd = STDOUT_FILENO;
        delta.open_mode = PIPE;
    }

    delta.open_mode |= WRITE;

    if (IS_MODE(delta.open_mode, MMAP))
        delta.max_buf_size = ((int)HEADER_SIZE < PAGE_SIZE ? PAGE_SIZE : (size_t)HEADER_SIZE);

    if (IS_MODE(delta.open_mode, DIRECT) || IS_MODE(delta.open_mode, PIPE))
    {
        delta.max_buf_size = (size_t)HEADER_SIZE;
        delta.buf_data = malloc(delta.max_buf_size);
    }

    delta.data_size = HEADER_SIZE;
    dev_truncate(&delta);

    map_buffer(&delta);

    strcpy(delta_header.recognize, MAGIC_DELTA);
    strcpy(delta_header.version, BSF_VERSION);
    delta_header.data_size = src.data_size;
    delta_header.block_size = param.block_size;
    delta_header.total_blocks = param.num_blocks;
    delta_header.timestamp = time(NULL);
    delta_header.hash_type = 0;
    memset(delta_header.padding, '\0', sizeof(delta_header.padding));

    if (IS_MODE(delta.open_mode, MMAP_W))
    {
        get_ptr(&delta);
        memcpy((void *)delta.ptr_w, (const void *)&delta_header, (size_t)sizeof(delta_header));
    }

    if (IS_MODE(delta.open_mode, DIRECT_W))
    {
        if (write_at_all(delta.fd, (const void *)&delta_header, (size_t)sizeof(delta_header), (off_t)0) < 0)
        {
            fprintf(stderr, "%s: error while writing to '%s' : %s\n", process_name, delta.path, strerror(errno));
            cleanup(EXIT_FAILURE);
        }
    }

    if (IS_MODE(delta.open_mode, PIPE_W) && !isatty(STDOUT_FILENO))
    {
        if (write_stream_all(delta.fd, (const void *)&delta_header, (size_t)sizeof(delta_header)) < 0)
        {
            fprintf(stderr, "%s: error while writing to stdout: %s\n", process_name, strerror(errno));
            cleanup(EXIT_FAILURE);
        }
    }

    delta.abs_off = delta.rel_off = HEADER_SIZE;
    sync_data(&delta);
}

void init_src_delta(void)
{
    if (delta.path != NULL)
    {
        delta.fd = open(delta.path, O_RDONLY);

        if (delta.fd < 0 || fstat(delta.fd, &delta.stat) < 0)
        {
            fprintf(stderr, "%s: unable to open delta \'%s\': %s\n",
                    process_name, delta.path, strerror(errno));
            cleanup(EXIT_FAILURE);
        }

        delta.data_size = lseek(delta.fd, 0, SEEK_END);
        lseek(delta.fd, 0, SEEK_SET);

        fprintf(flag.prst, "Delta file: '%s' has size of %s\n", delta.path, format_units(delta.data_size, true));

        if ((delta.data_size < HEADER_SIZE))
        {
            fprintf(stderr, "%s: delta file '%s' is invalid\n", process_name, delta.path);
            cleanup(EXIT_FAILURE);
        }
    }
    else
    {
        fprintf(flag.prst, "Data to apply-delta is read from STDIN\n");
        delta.fd = STDIN_FILENO;
        delta.open_mode = PIPE;
        delta.data_size = SIZE_MAX;
    }

    delta.open_mode |= READ;

    if (IS_MODE(delta.open_mode, MMAP))
        delta.max_buf_size = ((int)HEADER_SIZE < PAGE_SIZE ? PAGE_SIZE : (size_t)HEADER_SIZE);

    if (IS_MODE(delta.open_mode, DIRECT) || IS_MODE(delta.open_mode, PIPE))
    {
        delta.max_buf_size = (size_t)HEADER_SIZE;
        delta.buf_data = malloc(delta.max_buf_size);
    }

    map_buffer(&delta);
    delta_read_header();

    if (memcmp(delta_header.recognize, (const void *)(MAGIC_DELTA), sizeof(MAGIC_DELTA)) != 0 || delta_header.block_size < 1)
    {
        fprintf(stderr, "%s: delta data is invalid\n", process_name);
        cleanup(EXIT_FAILURE);
    }

    if (param.block_size != delta_header.block_size)
    {
        param.block_size = delta_header.block_size;
        fprintf(flag.prst, "Adjusting new block size from delta file: %s\n", format_units(delta_header.block_size, true));
    }

    param.num_blocks = delta_header.total_blocks;
    param.data_size = delta_header.data_size;
    delta.abs_off = delta.rel_off = HEADER_SIZE;
}
