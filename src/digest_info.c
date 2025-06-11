/*
 ./src/digest-info.c - this file is a part of program blocksync-fast

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

void digest_info(void)
{
	fprintf(flag.prst, "Operation mode: digest-info\n");

	if (digest.path == NULL)
	{
		fprintf(stderr,
				"%s - you need to specify the digest path\n",
				process_name);

		fprintf(flag.prst, "Try '%s --help' for more information.\n", process_name);

		cleanup(EXIT_FAILURE);
	}

	if (flag.mmap == 1)
	{
		fprintf(flag.prst, "Uses mmap as an alternative read and write method\n");
		digest.open_mode |= MMAP;
	}
	else
		digest.open_mode |= DIRECT;

	if (access(digest.path, F_OK) < 0 || (digest.fd = open(digest.path, O_RDONLY)) < 0)
	{

		fprintf(stderr, "%s: unable to open digest file \'%s\': %s\n", process_name, digest.path, strerror(errno));
		cleanup(EXIT_FAILURE);
	}

	digest.open_mode |= READ;
	digest.data_size = lseek(digest.fd, 0, SEEK_END);
	lseek(digest.fd, 0, SEEK_SET);

	size_t pref_buf_size = HEADER_SIZE;

	if (IS_MODE(digest.open_mode, MMAP))
		digest.max_buf_size = (pref_buf_size < getpagesize() ? getpagesize() : pref_buf_size);

	if (IS_MODE(digest.open_mode, DIRECT))
	{
		digest.max_buf_size = pref_buf_size;
		digest.buf_data = malloc(digest.max_buf_size);
	}

	off_t dds = digest.data_size;
	digest.data_size = HEADER_SIZE;
	map_buffer(&digest);

	if (digest.data_size < HEADER_SIZE || digest.buf_data == NULL)
	{
		fprintf(stderr, "%s: digest file '%s' is invalid\n", process_name, digest.path);
		cleanup(EXIT_FAILURE);
	}

	digest.data_size = dds;

	digest_read_header();

	if (memcmp(digest_header.recognize, (const void *)(MAGIC_DIGEST), sizeof(MAGIC_DIGEST)) != 0 || digest.data_size < HEADER_SIZE || digest_header.block_size < 1)
	{
		fprintf(stderr, "%s: digest file '%s' is invalid\n", process_name, digest.path);
		cleanup(EXIT_FAILURE);
	}

	fprintf(flag.prst, "Digest file: '%s' has size of %s\n", digest.path, format_units(digest.data_size, true));
	fprintf(flag.prst, "Program version: %s\n", digest_header.version);
	fprintf(flag.prst, "Device size: %s\n", format_units(digest_header.data_size, true));
	fprintf(flag.prst, "Block size: %s\n", format_units(digest_header.block_size, true));

	long num_blocks = (digest_header.data_size / digest_header.block_size) + (digest_header.data_size % digest_header.block_size > 0 ? 1 : 0);
	fprintf(flag.prst, "Number of blocks: %lu\n", num_blocks);

	time_t t = (time_t)digest_header.timestamp;
	struct tm *dt = localtime(&t);
	char timestr[40];

	strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", dt);
	fprintf(flag.prst, "Modify time: %s\n", timestr);

	for (int i = 0; i <= 1024; i++)
	{
		if (algos[i].value == 0)
		{
			fprintf(flag.prst, "Hash algo: unknown\n");
			break;
		}

		if (digest_header.hash_type == algos[i].value)
		{
			fprintf(flag.prst, "Hash algo: %s\n", algos[i].symbol);
			break;
		}
	}
}

void delta_info(void)
{
	fprintf(flag.prst, "Operation mode: delta-info\n");

	if (delta.path == NULL)
	{
		fprintf(stderr,
				"%s - you need to specify the delta path (--delta, -D)\n",
				process_name);

		fprintf(flag.prst, "Try '%s --help' for more information.\n", process_name);
		cleanup(EXIT_FAILURE);
	}

	if (flag.mmap == 1)
	{
		fprintf(flag.prst, "Uses mmap as an alternative read and write method\n");
		delta.open_mode |= MMAP;
	}
	else
		delta.open_mode |= DIRECT;

	if (access(delta.path, F_OK) < 0 || (delta.fd = open(delta.path, O_RDONLY)) < 0)
	{

		fprintf(stderr, "%s: unable to open delta file \'%s\': %s\n", process_name, delta.path, strerror(errno));
		cleanup(EXIT_FAILURE);
	}

	delta.open_mode |= READ;
	delta.data_size = lseek(delta.fd, 0, SEEK_END);
	lseek(delta.fd, 0, SEEK_SET);

	size_t pref_buf_size = HEADER_SIZE;

	if (IS_MODE(delta.open_mode, MMAP))
		delta.max_buf_size = (pref_buf_size < getpagesize() ? getpagesize() : pref_buf_size);

	if (IS_MODE(delta.open_mode, DIRECT))
	{
		delta.max_buf_size = pref_buf_size;
		delta.buf_data = malloc(delta.max_buf_size);
	}

	off_t dds = delta.data_size;
	delta.data_size = HEADER_SIZE;
	map_buffer(&delta);

	if (delta.data_size < HEADER_SIZE || delta.buf_data == NULL)
	{
		fprintf(stderr, "%s: delta file '%s' is invalid\n", process_name, delta.path);
		cleanup(EXIT_FAILURE);
	}

	delta.data_size = dds;

	delta_read_header();

	if (memcmp(delta_header.recognize, (const void *)(MAGIC_DELTA), sizeof(MAGIC_DELTA)) != 0 || delta.data_size < HEADER_SIZE || delta_header.block_size < 1)
	{
		fprintf(stderr, "%s: delta file '%s' is invalid\n", process_name, delta.path);
		cleanup(EXIT_FAILURE);
	}

	fprintf(flag.prst, "Delta file: '%s' has size of %s\n", delta.path, format_units(delta.data_size, true));
	fprintf(flag.prst, "Program version: %s\n", delta_header.version);
	fprintf(flag.prst, "Device size: %s\n", format_units(delta_header.data_size, true));
	fprintf(flag.prst, "Block size: %s\n", format_units(delta_header.block_size, true));

	long num_blocks = (delta_header.data_size / delta_header.block_size) + (delta_header.data_size % delta_header.block_size > 0 ? 1 : 0);
	fprintf(flag.prst, "Number of blocks: %lu\n", num_blocks);

	time_t t = (time_t)delta_header.timestamp;
	struct tm *dt = localtime(&t);
	char timestr[40];

	strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", dt);
	fprintf(flag.prst, "Create time: %s\n", timestr);
}
