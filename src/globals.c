/*
 ./src/globals.c - this file is a part of program blocksync-fast

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

int PAGE_SIZE = 4096;
struct dev src, dst, digest, delta = {NULL, -1, {}, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NONE};
struct bsf_header digest_header, delta_header = {"", "", 0, 0, 0, NONE};
struct oper oper = {0, 0, 0, 0, NULL, NULL};
struct flag flag = {BLOCKSYNC, 0, 0, 0, 0, 0, 0, 0, NULL};
struct prog prog = {0, 0, 0, 0, false, false, false, false, false, false, false, false};

char *process_name = PROGRAM_NAME;

const struct symbol_value_desc algos[] =
	{
		{"SHA1", GCRY_MD_SHA1, 20, LIBGCRYPT, "This is the SHA-1 algorithm which yields a message digest of 20 bytes."},
		{"RMD160", GCRY_MD_RMD160, 20, LIBGCRYPT, "This is the 160 bit version of the RIPE message digest (RIPE-MD-160). Like SHA-1 it also yields a digest of 20 bytes."},
		{"MD5", GCRY_MD_MD5, 16, LIBGCRYPT, "This is the well known MD5 algorithm, which yields a message digest of 16 bytes."},
		{"TIGER", GCRY_MD_TIGER, 24, LIBGCRYPT, "This is the TIGER/192 algorithm which yields a message digest of 24 bytes."},
		{"TIGER1", GCRY_MD_TIGER1, 24, LIBGCRYPT, "This is the TIGER variant as used by the NESSIE project."},
		{"TIGER2", GCRY_MD_TIGER2, 24, LIBGCRYPT, "This is another variant of TIGER with a different padding scheme."},
		{"SHA224", GCRY_MD_SHA224, 28, LIBGCRYPT, "This is the SHA-224 algorithm which yields a message digest of 28 bytes."},
		{"SHA256", GCRY_MD_SHA256, 32, LIBGCRYPT, "This is the SHA-256 algorithm which yields a message digest of 32 bytes."},
		{"SHA384", GCRY_MD_SHA384, 48, LIBGCRYPT, "This is the SHA-384 algorithm which yields a message digest of 48 bytes."},
		{"SHA512", GCRY_MD_SHA512, 64, LIBGCRYPT, "This is the SHA-512 algorithm which yields a message digest of 64 bytes."},
		{"SHA512_224", GCRY_MD_SHA512_224, 28, LIBGCRYPT, "This is the SHA-512/224 algorithm which yields a message digest of 28 bytes."},
		{"SHA512_256", GCRY_MD_SHA512_256, 32, LIBGCRYPT, "This is the SHA-512/256 algorithm which yields a message digest of 32 bytes."},
		{"SHA3_224", GCRY_MD_SHA3_224, 28, LIBGCRYPT, "This is the SHA3-224 algorithm which yields a message digest of 28 bytes."},
		{"SHA3_256", GCRY_MD_SHA3_256, 32, LIBGCRYPT, "This is the SHA3-256 algorithm which yields a message digest of 32 bytes."},
		{"SHA3_384", GCRY_MD_SHA3_384, 48, LIBGCRYPT, "This is the SHA3-384 algorithm which yields a message digest of 48 bytes."},
		{"SHA3_512", GCRY_MD_SHA3_512, 64, LIBGCRYPT, "This is the SHA3-512 algorithm which yields a message digest of 64 bytes."},

#ifdef HAVE_XXHASH
		{"CRC32", GCRY_MD_CRC32, 4, LIBGCRYPT, "This is the ISO 3309 and ITU-T V.42 cyclic redundancy check. It yields an output of 4 bytes."},
#else
		{"CRC32", GCRY_MD_CRC32, 4, LIBGCRYPT, "This is the ISO 3309 and ITU-T V.42 cyclic redundancy check. It yields an output of 4 bytes. (default)"},
#endif

		{"CRC32_RFC1510", GCRY_MD_CRC32_RFC1510, 4, LIBGCRYPT, "This is the above cyclic redundancy check function, as modified by RFC 1510. It yields an output of 4 bytes."},
		{"CRC24_RFC2440", GCRY_MD_CRC24_RFC2440, 3, LIBGCRYPT, "This is the OpenPGP cyclic redundancy check function. It yields an output of 3 bytes."},
		{"WHIRLPOOL", GCRY_MD_WHIRLPOOL, 64, LIBGCRYPT, "This is the Whirlpool algorithm which yields a message digest of 64 bytes."},
		{"GOSTR3411_94", GCRY_MD_GOSTR3411_94, 32, LIBGCRYPT, "This is the hash algorithm which yields a message digest of 32 bytes."},
		{"STRIBOG256", GCRY_MD_STRIBOG256, 32, LIBGCRYPT, "This is the 256-bit version of hash algorithm which yields a message digest of 32 bytes."},
		{"STRIBOG512", GCRY_MD_STRIBOG512, 64, LIBGCRYPT, "This is the 512-bit version of hash algorithm which yields a message digest of 64 bytes."},
		{"BLAKE2B_160", GCRY_MD_BLAKE2B_160, 20, LIBGCRYPT, "This is the BLAKE2b-160 algorithm which yields a message digest of 20 bytes."},
		{"BLAKE2B_256", GCRY_MD_BLAKE2B_256, 32, LIBGCRYPT, "This is the BLAKE2b-256 algorithm which yields a message digest of 32 bytes."},
		{"BLAKE2B_384", GCRY_MD_BLAKE2B_384, 48, LIBGCRYPT, "This is the BLAKE2b-384 algorithm which yields a message digest of 48 bytes."},
		{"BLAKE2B_512", GCRY_MD_BLAKE2B_512, 64, LIBGCRYPT, "This is the BLAKE2b-512 algorithm which yields a message digest of 64 bytes."},
		{"BLAKE2S_128", GCRY_MD_BLAKE2S_128, 16, LIBGCRYPT, "This is the BLAKE2s-128 algorithm which yields a message digest of 16 bytes."},
		{"BLAKE2S_160", GCRY_MD_BLAKE2S_160, 20, LIBGCRYPT, "This is the BLAKE2s-160 algorithm which yields a message digest of 20 bytes."},
		{"BLAKE2S_224", GCRY_MD_BLAKE2S_224, 28, LIBGCRYPT, "This is the BLAKE2s-224 algorithm which yields a message digest of 28 bytes."},
		{"BLAKE2S_256", GCRY_MD_BLAKE2S_256, 32, LIBGCRYPT, "This is the BLAKE2s-256 algorithm which yields a message digest of 32 bytes."},
		{"SM3", GCRY_MD_SM3, 32, LIBGCRYPT, "This is the SM3 algorithm which yields a message digest of 32 bytes."},

#ifdef HAVE_XXHASH
		{"XXH32", XXHASH_MD_XXH32, 4, LIBXXHASH, "This is the 32-bit wariant of xxHash algorithm which yields a message digest of 4 bytes."},
		{"XXH64", XXHASH_MD_XXH64, 8, LIBXXHASH, "This is the 64-bit wariant of xxHash algorithm which yields a message digest of 8 bytes."},
		{"XXH3LOW", XXHASH_MD_XXH3LOW, 4, LIBXXHASH, "This is the fast (SSE2,AVX2) low 32-bits parts from 64-bit wariant of xxHash algorithm which yields a message digest of 4 bytes. (default)"},
		{"XXH3", XXHASH_MD_XXH3, 8, LIBXXHASH, "This is the fast (SSE2,AVX2) 64-bit wariant of xxHash algorithm which yields a message digest of 8 bytes."},
		{"XXH128", XXHASH_MD_XXH128, 16, LIBXXHASH, "This is the fast (SSE2,AVX2) 128-bit wariant of xxHash algorithm which yields a message digest of 16 bytes."},
#endif

		{"", 0, 0, 0, ""}};

struct param param = {NULL, D_BLOCK_SIZE, D_BUFFER_SIZE, 0, NULL, 0, 0, 1, "", false, NULL, algos[D_ALGO]};

void get_ptr(struct dev *dev)
{
	if (IS_MODE(dev->open_mode, READ))
		dev->ptr_r = dev->buf_data + (dev->rel_off);

	if (IS_MODE(dev->open_mode, WRITE))
		dev->ptr_w = dev->buf_data + (dev->rel_off);
}

void map_buffer(struct dev *dev)
{
	if (dev->buf_data != NULL && IS_MODE(dev->open_mode, MMAP))
		munmap(dev->buf_data, dev->buf_size);

	dev->rel_off = 0;
	off_t abs_off = dev->abs_off;

	if (IS_MODE(dev->open_mode, MMAP))
	{
		int pflags = (IS_MODE(dev->open_mode, READ) ? PROT_READ : 0) | (IS_MODE(dev->open_mode, WRITE) ? PROT_WRITE : 0);

		if (dev->abs_off % PAGE_SIZE > 0)
		{
			dev->rel_off = dev->abs_off % PAGE_SIZE;
			abs_off = (dev->abs_off / PAGE_SIZE) * PAGE_SIZE;
		}

		dev->buf_size = (dev->data_size - abs_off) >= dev->max_buf_size ? dev->max_buf_size : (dev->data_size - abs_off);
		dev->buf_data = (char *)mmap(NULL, dev->buf_size, pflags, MAP_SHARED, dev->fd, abs_off);

		if (dev->buf_data == MAP_FAILED)
		{
			fprintf(stderr, "%s: mmap() device '%s' error '%m' (%d)\n", process_name, dev->path, errno);
			cleanup(EXIT_FAILURE);
		}
	}

	if (IS_MODE(dev->open_mode, DIRECT))
		dev->buf_size = (dev->data_size - dev->abs_off) >= dev->max_buf_size ? dev->max_buf_size : (dev->data_size - dev->abs_off);

	if (IS_MODE(dev->open_mode, DIRECT_R))
	{
		if (pread(dev->fd, dev->buf_data, dev->buf_size, dev->abs_off) < 0)
		{
			fprintf(stderr, "%s: error while reading from '%s' : %s\n", process_name, dev->path, strerror(errno));
			cleanup(EXIT_FAILURE);
		}
	}

	if (IS_MODE(dev->open_mode, PIPE_R))
	{
		dev->buf_size = (dev->data_size - abs_off) >= dev->max_buf_size ? dev->max_buf_size : (dev->data_size - abs_off);

		size_t rbytes = 0;
		size_t tbytes = 0;

		while (tbytes < dev->buf_size)
		{
			rbytes = read(dev->fd, (dev->buf_data + tbytes), (dev->buf_size - tbytes));

			if (rbytes < 0)
			{
				fprintf(stderr, "%s: error while reading from stdin : %s\n", process_name, dev->path, strerror(errno));
				cleanup(EXIT_FAILURE);
			}

			if (rbytes == 0)
			{
				dev->buf_size = tbytes;
				dev->data_size = abs_off + tbytes;
				break;
			}

			tbytes += rbytes;
		}
	}

	dev->mov_off = dev->rel_off;
	dev->buf_off = abs_off + dev->buf_size;
}

bool check_buffer_reload(struct dev *dev)
{
	bool dev_reload = false;

	if (IS_MODE(dev->open_mode, DIRECT))
		dev_reload = dev->abs_off >= dev->buf_off || dev->buf_off == 0;

	if (IS_MODE(dev->open_mode, MMAP))
		dev_reload = (dev->rel_off + dev->block_size) >= dev->buf_size || dev->buf_off == 0;

	if (IS_MODE(dev->open_mode, PIPE))
		dev_reload = dev->abs_off >= dev->buf_off || dev->buf_off == 0;

	return dev_reload;
}

void sync_data(struct dev *dev)
{
	if (flag.write_sync == 1 && dev->buf_data != NULL)
	{
		if (IS_MODE(dev->open_mode, DIRECT_W))
			fsync(dev->fd);

		if (IS_MODE(dev->open_mode, MMAP_W))
			msync(dev->buf_data, dev->buf_size, MS_SYNC);
	}
}

void blocksync_dev_wri_flush(size_t flush)
{
	if (oper.dev_wri_buf_size > 0)
	{
		if (!BIT_SET(flag.dont_write, 1))
		{
			off_t wri_buf_off = oper.dev_wri_buf_size + flush;

			if (IS_MODE(dst.open_mode, MMAP_W))
			{
				void *ptr_dst = dst.buf_data + (dst.rel_off - wri_buf_off);
				const void *ptr_src = src.buf_data + (src.rel_off - wri_buf_off);
				memcpy(ptr_dst, ptr_src, oper.dev_wri_buf_size);
			}

			if (IS_MODE(dst.open_mode, DIRECT_W))
			{
				off_t rel_buf_off = src.rel_off - wri_buf_off;
				off_t abs_buf_off = dst.abs_off - wri_buf_off;
				const void *ptr = src.buf_data + rel_buf_off;
				if (pwrite(dst.fd, ptr, oper.dev_wri_buf_size, abs_buf_off) < 0)
				{
					fprintf(stderr, "%s: error while writing to '%s' : %s\n", process_name, dst.path, strerror(errno));
					cleanup(EXIT_FAILURE);
				}
			}
		}

		oper.dev_wri_buf_size = 0;
	}
}

void digest_wri_flush(size_t flush)
{
	if (oper.digest_wri_buf_size > 0)
	{
		if (!BIT_SET(flag.dont_write, 0))
		{
			off_t wri_buf_off = oper.digest_wri_buf_size + flush;

			if (IS_MODE(digest.open_mode, MMAP_W))
			{
				void *ptr_digest = digest.buf_data + (digest.rel_off - wri_buf_off);
				const void *ptr_hash_buf = oper.hash_buf + (digest.rel_off - wri_buf_off - digest.mov_off);
				memcpy(ptr_digest, ptr_hash_buf, oper.digest_wri_buf_size);
			}

			if (IS_MODE(digest.open_mode, DIRECT_W))
			{
				off_t rel_buf_off = digest.rel_off - wri_buf_off;
				off_t abs_buf_off = digest.abs_off - wri_buf_off;
				const void *ptr = oper.hash_buf + (rel_buf_off - digest.mov_off);
				if (pwrite(digest.fd, ptr, oper.digest_wri_buf_size, abs_buf_off) < 0)
				{
					fprintf(stderr, "%s: error while writing to '%s' : %s\n", process_name, digest.path, strerror(errno));
					cleanup(EXIT_FAILURE);
				}
			}

			if (IS_MODE(digest.open_mode, PIPE_W))
			{
				off_t rel_buf_off = digest.rel_off - wri_buf_off;
				const void *ptr = oper.hash_buf + (rel_buf_off - digest.mov_off);
				if (write(digest.fd, ptr, oper.digest_wri_buf_size) < 0)
				{
					fprintf(stderr, "%s: error while writing to stdout: %s\n", process_name, strerror(errno));
					cleanup(EXIT_FAILURE);
				}
			}
		}

		oper.digest_wri_buf_size = 0;
	}
}

void dev_truncate(struct dev *dev)
{
	if (S_ISREG(dev->stat.st_mode) && ftruncate(dev->fd, dev->data_size) < 0)
	{
		fprintf(stderr, "%s: error while truncating '%s' : %s\n", process_name, dev->path, strerror(errno));
		cleanup(EXIT_FAILURE);
	}
}

void freedev(struct dev *dev)
{
	if (flag.write_sync == 1 && dev->buf_data != NULL)
	{
		if (IS_MODE(dev->open_mode, DIRECT_W))
			fsync(dev->fd);

		if (IS_MODE(dev->open_mode, MMAP_W))
			msync(dev->buf_data, dev->buf_size, MS_SYNC);
	}

	if (IS_MODE(dev->open_mode, MMAP) && dev->buf_data != NULL)
		munmap(dev->buf_data, dev->buf_size);

	if (IS_MODE(dev->open_mode, DIRECT) && dev->buf_data != NULL)
		free(dev->buf_data);

	if (IS_MODE(dev->open_mode, PIPE) && dev->buf_data != NULL)
		free(dev->buf_data);

	if (dev->fd >= 0)
		close(dev->fd);
}

void hash_init(int algo, int lib)
{
	if (lib == LIBGCRYPT)
	{
		if (!gcry_check_version(NEED_LIBGCRYPT_VERSION))
		{
			fprintf(stderr, "%s: libgcrypt is too old (need %s, have %s)\n",
					process_name, NEED_LIBGCRYPT_VERSION, gcry_check_version(NULL));
			cleanup(EXIT_FAILURE);
		}

		gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
		gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

		if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P))
		{
			fprintf(stderr, "%s: libgcrypt has not been initialized\n",
					process_name);
			cleanup(EXIT_FAILURE);
		}

		gcry_error_t err;
		err = gcry_md_open(&oper.gcrypt_state, algo, 0);

		if (err)
		{
			fprintf(stderr, "%s: Error opening hash context: %s\n", process_name, gcry_strerror(err));
			cleanup(EXIT_FAILURE);
		}
	}
#ifdef HAVE_XXHASH
	else if (lib == LIBXXHASH)
		switch (algo)
		{
		case XXHASH_MD_XXH32:
			oper.xxhash_state.xxh32 = XXH32_createState();
			break;

		case XXHASH_MD_XXH64:
			oper.xxhash_state.xxh64 = XXH64_createState();
			break;

		case XXHASH_MD_XXH3LOW:
		case XXHASH_MD_XXH3:
		case XXHASH_MD_XXH128:
			oper.xxhash_state.xxh3 = XXH3_createState();
			break;
		}
#endif
}

void *hash_alloc(size_t size, int lib)
{
	if (lib == LIBGCRYPT)
		return gcry_malloc(size);

	else
		return malloc(size);
}

void hash_buffer(int algo, int lib, int hash_size, void *digest, const void *buffer, size_t size)
{
	if (lib == LIBGCRYPT)
	{
		gcry_md_write(oper.gcrypt_state, buffer, size);
		memcpy(digest, gcry_md_read(oper.gcrypt_state, algo), hash_size);
		gcry_md_reset(oper.gcrypt_state);
	}

#ifdef HAVE_XXHASH
	else if (lib == LIBXXHASH)
		switch (algo)
		{
		case XXHASH_MD_XXH32:
			XXH32_reset(oper.xxhash_state.xxh32, 0);
			XXH32_update(oper.xxhash_state.xxh32, buffer, size);
			XXH32_canonicalFromHash(digest, XXH32_digest(oper.xxhash_state.xxh32));
			break;

		case XXHASH_MD_XXH64:
			XXH64_reset(oper.xxhash_state.xxh64, 0);
			XXH64_update(oper.xxhash_state.xxh64, buffer, size);
			XXH64_canonicalFromHash(digest, XXH64_digest(oper.xxhash_state.xxh64));
			break;

		case XXHASH_MD_XXH3LOW:
		case XXHASH_MD_XXH3:
			XXH3_64bits_reset(oper.xxhash_state.xxh3);
			XXH3_64bits_update(oper.xxhash_state.xxh3, buffer, size);
			if (algo == XXHASH_MD_XXH3LOW)
				XXH32_canonicalFromHash(digest, (uint32_t)(XXH3_64bits_digest(oper.xxhash_state.xxh3) & 0xFFFFFFFF));
			else
				XXH64_canonicalFromHash(digest, XXH3_64bits_digest(oper.xxhash_state.xxh3));
			break;

		case XXHASH_MD_XXH128:
			XXH3_128bits_reset(oper.xxhash_state.xxh3);
			XXH3_128bits_update(oper.xxhash_state.xxh3, buffer, size);
			XXH128_canonicalFromHash(digest, XXH3_128bits_digest(oper.xxhash_state.xxh3));
			break;
		}
#endif
}

void hash_free(int algo, int lib, void *buf)
{
	if (buf == NULL)
		return;

	if (lib == LIBGCRYPT)
	{
		gcry_md_close(oper.gcrypt_state);
		gcry_free(buf);
	}
#ifdef HAVE_XXHASH
	else if (lib == LIBXXHASH)
	{
		switch (algo)
		{
		case XXHASH_MD_XXH32:
			XXH32_freeState(oper.xxhash_state.xxh32);
			break;

		case XXHASH_MD_XXH64:
			XXH64_freeState(oper.xxhash_state.xxh64);
			break;

		case XXHASH_MD_XXH3LOW:
		case XXHASH_MD_XXH3:
		case XXHASH_MD_XXH128:
			XXH3_freeState(oper.xxhash_state.xxh3);
			break;
		}

		free(buf);
	}
#endif
}

void makedelta_wri_flush_buf()
{
	if (oper.delta_wri_buf_size > 0)
	{
		if (IS_MODE(delta.open_mode, MMAP_W))
		{
			void *ptr_delta = delta.buf_data + (delta.rel_off - (off_t)oper.delta_wri_buf_size);
			const void *ptr_delta_buf = oper.delta_buf + (delta.rel_off - (off_t)oper.delta_wri_buf_size - delta.mov_off);
			memcpy(ptr_delta, ptr_delta_buf, oper.delta_wri_buf_size);
		}

		if (IS_MODE(delta.open_mode, DIRECT_W))
		{
			off_t abs_buf_off = delta.abs_off - oper.delta_wri_buf_size;

			if (pwrite(delta.fd, (const void *)oper.delta_buf, oper.delta_wri_buf_size, abs_buf_off) < 0)
			{
				fprintf(stderr, "%s: error while writing to '%s' : %s\n", process_name, delta.path, strerror(errno));
				cleanup(EXIT_FAILURE);
			}
		}

		if (IS_MODE(delta.open_mode, PIPE_W))
		{
			if (write(delta.fd, (const void *)oper.delta_buf, oper.delta_wri_buf_size) < 0)
			{
				fprintf(stderr, "%s: error while writing to stdout: %s\n", process_name, strerror(errno));
				cleanup(EXIT_FAILURE);
			}
		}

		oper.delta_wri_buf_size = 0;
	}
}

void applydelta_wri_flush_buf(size_t ahead)
{
	if (oper.delta_wri_buf_size > 0)
	{
		if (!BIT_SET(flag.dont_write, 0))
		{

			off_t wri_buf_off = oper.delta_wri_buf_size - ahead;

			if (IS_MODE(dst.open_mode, MMAP_W))
			{
				void *ptr_dst = dst.buf_data + (dst.rel_off - wri_buf_off);
				const void *ptr_delta_buf = oper.delta_buf;
				memcpy(ptr_dst, ptr_delta_buf, oper.delta_wri_buf_size);
			}

			if (IS_MODE(dst.open_mode, DIRECT_W))
			{
				off_t abs_buf_off = dst.abs_off - wri_buf_off;

				if (pwrite(dst.fd, (const void *)oper.delta_buf, oper.delta_wri_buf_size, abs_buf_off) < 0)
				{
					fprintf(stderr, "%s: error while writing to '%s' : %s\n", process_name, dst.path, strerror(errno));
					cleanup(EXIT_FAILURE);
				}
			}
		}

		oper.delta_wri_buf_size = 0;
	}
}

void oper_delta_buf_free()
{
	if (oper.delta_buf != NULL)
		free(oper.delta_buf);
}

void cleanup(int result)
{
	freedev(&src);
	freedev(&dst);
	freedev(&digest);
	freedev(&delta);
	hash_free(param.algo.value, param.algo.library, oper.hash_buf);
	oper_delta_buf_free();
	exit(result);
}