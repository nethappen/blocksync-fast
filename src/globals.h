/*
 ./src/globals.h - this file is a part of program blocksync-fast

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

#ifndef GLOBALS_H
#define GLOBALS_H

#define PROGRAM_NAME "blocksync-fast"
#define AUTHORS \
	"Marcin Koczwara <mk@nethorizon.pl>"

#define BSF_VERSION "1.03"
#define COPYRIGHT "Copyright (C) 2023 " AUTHORS
#define LICENSE "Licensed under the Apache License, Version 2.0; <http://www.apache.org/licenses/LICENSE-2.0>"
#define FREE "This is free software: distributed on an \"AS IS\" BASIS,"
#define WARRANTY "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND"

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <getopt.h>

#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <time.h>		// time
#include <math.h>		// ceil
#include <sys/uio.h>	//readv writev
#include <sys/param.h>	// max
#include <sys/random.h> // random
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <libgen.h>

#include <gcrypt.h>
#include <unistd.h>

#include "common.h"
#include "utils.h"

#define LIBGCRYPT (1)
#define LIBXXHASH (2)

#ifdef HAVE_XXHASH
#include <xxhash.h>
enum xxhash_algos
{
	XXHASH_MD_XXH32 = 2010,
	XXHASH_MD_XXH64 = 2020,
	XXHASH_MD_XXH3LOW = 2029,
	XXHASH_MD_XXH3 = 2030,
	XXHASH_MD_XXH128 = 2040,
};
#define D_ALGO 34
#else
#define D_ALGO 16
#endif

#define _FILE_OFFSET_BITS 64
#define NEED_LIBGCRYPT_VERSION ("1.9.0")

extern int PAGE_SIZE;

#define D_BLOCK_SIZE (4 * 1024)			// 4KiB
#define D_BUFFER_SIZE (2 * 1024 * 1024) // 2 MiB
#define HEADER_SIZE (512)

#define MAGIC_NUMBER "!BSF#"
#define MAGIC_DIGEST MAGIC_NUMBER "DIG"
#define MAGIC_DELTA MAGIC_NUMBER "DELTA"

#define BIT_SET(v, p) ((v) & (1 << (p)))
#define IS_MODE(v, p) (((v) & (p)) == (p))

extern char *process_name;

extern struct dev
{
	const char *path;
	int fd;
	struct stat stat;
	off_t abs_off, buf_off, rel_off, mov_off;
	size_t data_size;
	size_t block_size;
	size_t buf_size;
	size_t max_buf_size;
	char *buf_data;
	const void *ptr_r;
	void *ptr_w;
	enum
	{
		NONE = 0,
		READ = 1,	  // 00001
		WRITE = 2,	  // 00010
		DIRECT = 4,	  // 00100
		DIRECT_R = 5, // 00101
		DIRECT_W = 6, // 00110
		MMAP = 8,	  // 01000
		MMAP_R = 9,	  // 01001
		MMAP_W = 10,  // 01010
		PIPE = 16,	  // 10000
		PIPE_R = 17,  // 10001
		PIPE_W = 18,  // 10010
	} open_mode;
} src, dst, digest, delta;

extern struct bsf_header
{
	char recognize[16];
	char version[8];
	uint64_t data_size;
	uint64_t block_size;
	uint64_t total_blocks;
	uint64_t timestamp;
	uint64_t hash_type;
	char padding[448];
} digest_header, delta_header;

extern struct symbol_value_desc
{
	char symbol[16];
	int value;
	int size;
	int library;
	char desc[192];
} algo;

extern const struct symbol_value_desc algos[];

extern struct oper
{
	size_t num_block;
	size_t dev_wri_buf_size;
	size_t digest_wri_buf_size;
	size_t delta_wri_buf_size;
	char *hash_buf;
	char *delta_buf;
	gcry_md_hd_t gcrypt_state;
#ifdef HAVE_XXHASH
	union xxhash_state
	{
		XXH32_state_t *xxh32;
		XXH64_state_t *xxh64;
		XXH3_state_t *xxh3;
	} xxhash_state;
#endif
} oper;

extern struct param
{
	char *h_block_size;
	size_t block_size;
	size_t max_buf_size;
	size_t num_blocks;
	size_t data_size;
	bool hash_use;
	const char *hash_algo;
	struct symbol_value_desc algo;
} param;

enum oper_modes
{
	BLOCKSYNC,
	BENCHMARK,
	DIGESTINFO,
	DELTAINFO,
	MAKEDELTA,
	APPLYDELTA,
	MAKEDIGEST,
};

extern struct flag
{
	int oper_mode;
	int progress;
	int force;
	int mmap;
	int write_sync;
	int dont_write;
	int silent;
	int no_compare;
	FILE *prst;
} flag;

extern struct prog
{
	size_t wri_bytes;
	size_t wri_blocks;
	char p_per, c_per;
	bool p_dst_wri, c_dst_wri;
	bool p_dst_mat, c_dst_mat;
	bool p_dig_wri, c_dig_wri;
	bool p_dig_mat, c_dig_mat;
} prog;

void get_ptr(struct dev *dev);
void map_buffer(struct dev *dev);
bool check_buffer_reload(struct dev *dev);
void sync_data(struct dev *dev);
void blocksync_dev_wri_flush(size_t flush);
void digest_wri_flush(size_t flush);
void dev_truncate(struct dev *dev);
void freedev(struct dev *dev);
void hash_init(int algo, int lib);
void *hash_alloc(size_t size, int lib);
void hash_buffer(int algo, int lib, int hash_size, void *digest, const void *buffer, size_t size);
void hash_free(int algo, int lib, void *buf);
void makedelta_wri_flush_buf();
void applydelta_wri_flush_buf();
void oper_delta_buf_free();
void cleanup(int result);

#endif