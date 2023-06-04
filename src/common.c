/*
 ./src/common.c - this file is a part of program blocksync-fast

 Copyright (C) 2023 Marcin Koczwara <mk@nethorizon.pl>

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

void digest_read_header(void)
{
    get_ptr(&digest);
    memcpy((char *)&digest_header.recognize, (const void *)digest.ptr_r, sizeof(digest_header.recognize));
    digest.rel_off += sizeof(digest_header.recognize);

    get_ptr(&digest);
    memcpy((char *)&digest_header.version, (const void *)digest.ptr_r, sizeof(digest_header.version));
    digest.rel_off += sizeof(digest_header.version);

    get_ptr(&digest);
    memcpy((char *)&digest_header.data_size, (const void *)digest.ptr_r, sizeof(digest_header.data_size));
    digest.rel_off += sizeof(digest_header.data_size);

    get_ptr(&digest);
    memcpy((char *)&digest_header.block_size, (const void *)digest.ptr_r, sizeof(digest_header.block_size));
    digest.rel_off += sizeof(digest_header.block_size);

    get_ptr(&digest);
    memcpy((char *)&digest_header.total_blocks, (const void *)digest.ptr_r, sizeof(digest_header.total_blocks));
    digest.rel_off += sizeof(digest_header.total_blocks);

    get_ptr(&digest);
    memcpy((char *)&digest_header.timestamp, (const void *)digest.ptr_r, sizeof(digest_header.timestamp));
    digest.rel_off += sizeof(digest_header.timestamp);

    get_ptr(&digest);
    memcpy((char *)&digest_header.hash_type, (const void *)digest.ptr_r, sizeof(digest_header.hash_type));
    digest.rel_off += sizeof(digest_header.hash_type);
}

void delta_read_header(void)
{
    get_ptr(&delta);
    memcpy((char *)&delta_header.recognize, (const void *)delta.ptr_r, sizeof(delta_header.recognize));
    delta.rel_off += sizeof(delta_header.recognize);

    get_ptr(&delta);
    memcpy((char *)&delta_header.version, (const void *)delta.ptr_r, sizeof(delta_header.version));
    delta.rel_off += sizeof(delta_header.version);

    get_ptr(&delta);
    memcpy((char *)&delta_header.data_size, (const void *)delta.ptr_r, sizeof(delta_header.data_size));
    delta.rel_off += sizeof(delta_header.data_size);

    get_ptr(&delta);
    memcpy((char *)&delta_header.block_size, (const void *)delta.ptr_r, sizeof(delta_header.block_size));
    delta.rel_off += sizeof(delta_header.block_size);

    get_ptr(&delta);
    memcpy((char *)&delta_header.total_blocks, (const void *)delta.ptr_r, sizeof(delta_header.total_blocks));
    delta.rel_off += sizeof(delta_header.total_blocks);

    get_ptr(&delta);
    memcpy((char *)&delta_header.timestamp, (const void *)delta.ptr_r, sizeof(delta_header.timestamp));
    delta.rel_off += sizeof(delta_header.timestamp);

    get_ptr(&delta);
    memcpy((char *)&delta_header.hash_type, (const void *)delta.ptr_r, sizeof(delta_header.hash_type));
    delta.rel_off += sizeof(delta_header.hash_type);
}

bool adjust_buffer(size_t *max_buf_size, size_t block_size)
{
    size_t t_max_buf_size;
    bool buf_adj = false;

    if (flag.mmap == 0)
    {
        t_max_buf_size = block_size * 2;

        if (*max_buf_size < t_max_buf_size)
        {
            *max_buf_size = t_max_buf_size;
            buf_adj = true;
        }

        if ((*max_buf_size % block_size) != 0)
        {
            *max_buf_size = (*max_buf_size / block_size) * block_size;
            buf_adj = true;
        }
    }

    if (flag.mmap == 1)
    {
        t_max_buf_size = MAX(p2r(block_size), PAGE_SIZE) * 2;

        if (*max_buf_size < t_max_buf_size)
        {
            *max_buf_size = t_max_buf_size;
            buf_adj = true;
        }

        if (*max_buf_size % PAGE_SIZE != 0)
        {
            *max_buf_size = p2r(((*max_buf_size / PAGE_SIZE) * PAGE_SIZE) + PAGE_SIZE);
            buf_adj = true;
        }
    }

    return buf_adj;
}
