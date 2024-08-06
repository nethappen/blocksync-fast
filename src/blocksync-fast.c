/*
 ./src/blocksync-fast.c - this file is a part of program blocksync-fast

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
#include "init.h"

void print_version(void)
{
	fprintf(flag.prst, "%s version %s, %s\n", PROGRAM_NAME, BSF_VERSION, COPYRIGHT);
	fprintf(flag.prst, "%s\n", LICENSE);
	fprintf(flag.prst, "%s %s\n", FREE, WARRANTY);
}

void print_help(void)
{
	print_version();

	fprintf(flag.prst, "\n");

	fprintf(flag.prst, "Usage:\n",
			process_name);

	fprintf(flag.prst, " %s [options]\n",
			process_name);

	fprintf(flag.prst, " %s -s <src_device> -d <dst_device> [-f <digest_file>] [options]\n",
			process_name);

	fprintf(flag.prst, " %s -s <src_device> [-f <digest_file>] --make-delta -D <delta_file> [options]\n",
			process_name);

	fprintf(flag.prst, " %s -d <dst_device> --apply-delta -D <delta_file> [options]\n",
			process_name);

	fprintf(flag.prst, "\nOptions:\n"

					   "-s, --src=PATH\n"
					   "  Source block device or disk image\n"
					   "\n"

					   "-d, --dst=PATH\n"
					   "  Destination block device or disk image\n"
					   "\n"

					   "-S, --size=N[KMG]\n"
					   "  Data size in N bytes for STDIN data or override disk image size\n"
					   "\n"

					   "--make-digest\n"
					   "  Creates only digest file\n"
					   "\n"

					   "-f, --digest=PATH\n"
					   "  Digest file stores checksums of the blocks from sync\n"
					   "\n"

					   "--make-delta\n"
					   "  Creates a delta file from src\n"
					   "\n"

					   "--apply-delta\n"
					   "  Applies a delta file to dst\n"
					   "\n"

					   "-D, --delta=PATH\n"
					   "  Delta file path. If none, data write to stdout or read from stdin\n"
					   "\n"

					   "-b, --block-size=N[KMG]\n"
					   "  Block size in N bytes for writing and checksum calculations\n"
					   "  (default:4K)\n"
					   "\n"

					   "-a, --algo=ALGO\n"
					   "  Cryptographic hash algorithm which is used to compute checksum to compare blocks\n"
#ifdef HAVE_XXHASH
					   "  (default:XXH3LOW)\n"
#else
					   "  (default:CRC32)\n"
#endif
					   "\n"

					   "-l, --list-algos\n"
					   "  It prints all supported hash algorithms\n"
					   "\n"

					   "--benchmark-algos\n"
					   "  Benchmark all supported hash algorithms\n"
					   "\n"

					   "--digest-info\n"
					   "  Checks digest file, prints info and exit\n"
					   "\n"

					   "--delta-info\n"
					   "  Checks delta file, prints info and exit\n"
					   "\n"

					   "--buffer-size=N[KMG]\n"
					   "  Size of the buffer in N bytes for processing data per device\n"
					   "  (default:2M)\n"
					   "\n"

					   "--progress, --show-progress\n"
					   "  Show current progress while syncing\n"
					   "\n"

					   "--progress-detail, --show-progress-detail\n"
					   "  Show more detailed progress which generates a lot of writes on the console\n"
					   "\n"

					   "--mmap\n"
					   "  Use a system mmap instead of direct read and write method\n"
					   "\n"

					   "--no-compare\n"
					   "  Copy all data from src to dst without comparing differences\n"
					   "\n"

					   "--sync-writes\n"
					   "  Immediately flushes and writes data to the disk specified at --buffer-size\n"
					   "\n"

					   "--dont-write\n"
					   "  Perform dry run with no updates to target and digest file\n"
					   "\n"

					   "--dont-write-target\n"
					   "  Perform run with no updates only to target device\n"
					   "\n"

					   "--dont-write-digest\n"
					   "  Perform run with no updates only to digest file\n"
					   "\n"

					   "--force\n"
					   "  Allows to overwrite files and override parameters which was generated before\n"
					   "\n"

					   "--silent\n"
					   "  Doesn't print any messages\n"
					   "\n"

					   "--help, -h\n"
					   "  Show this help message\n"
					   "\n"

					   "--version, -V\n"
					   "  Show version\n"
					   "\n"

					   "This program compares and synchronizes block devices using fast and efficient methods.\n"
					   "Digest can be used to store checksums of data blocks from previous synchronization\n"
					   "to speed up synchronize process and avoid read operations from target block device.\n"
					   "Program can also create delta files that contains differences between block devices.\n");
}

void print_algos(void)
{
	fprintf(flag.prst, "Usage: %s -s <src_device> -d <dst_device> -a <algo> [-f <digest_file>] [--progress] [options]\n",
			process_name);

	fprintf(flag.prst, "\nSupported hash algorithms:\n");

	for (int i = 0; i <= 1024; i++)
	{
		if (algos[i].value == 0)
			break;

		fprintf(flag.prst, "  %s - %s\n", algos[i].symbol, algos[i].desc);
	}
}

void print_progress(void)
{
	prog.c_per = floor((((float)(oper.num_block + 1) / (float)param.num_blocks) * 100) * param.pro_fact) / param.pro_fact;

	if (prog.p_per != prog.c_per || oper.num_block < 1) {
		fprintf(flag.prst, param.pro_form, prog.c_per);
		fflush(stdout);		
	}

	prog.p_per = prog.c_per;
}

void print_detail_progress_row(size_t cur_block, size_t num_blocks, bool dig_mat, bool dig_wri, bool dst_mat, bool dst_wri)
{
	bool target_write = false;
	bool target_match = false;

	if (flag.oper_mode == BLOCKSYNC || flag.oper_mode == APPLYDELTA)
		target_write = true;

	if (flag.oper_mode == BLOCKSYNC || flag.oper_mode == MAKEDELTA)
		target_match = true;

	fprintf(flag.prst, "\33[2K\r");
	fprintf(flag.prst, "\rBlock: %10zu/%zu"
					   "%s"
					   "%s"
					   "%s"
					   "%s",
			cur_block,
			num_blocks,
			(dig_mat ? "\tDigest:Match" : ""),
			(dig_wri ? "\tDigest:Write" : ""),
			(dst_mat ? (target_match ? "\tTarget:Match" : "\tTarget:Seek") : ""),
			(dst_wri ? (target_write ? "\tTarget:Write" : "\tDelta:Write") : ""));

	fflush(stdout);
}

void print_detail_progress(void)
{
	bool p_c = ((prog.p_dst_wri != prog.c_dst_wri || prog.p_dig_wri != prog.c_dig_wri || prog.p_dig_mat != prog.c_dig_mat || prog.p_dst_mat != prog.c_dst_mat) && (prog.p_dst_wri || prog.p_dig_wri || prog.p_dig_mat || prog.p_dst_mat) ? true : false);

	if (prog.p_per != prog.c_per || p_c)
		print_detail_progress_row((oper.num_block), param.num_blocks, prog.p_dig_mat, prog.p_dig_wri, prog.p_dst_mat, prog.p_dst_wri);

	prog.c_per = (((float)(oper.num_block + 1) / (float)param.num_blocks) * 100);

	if (oper.num_block > 0 && p_c)
		fprintf(flag.prst, "\n");

	if (prog.p_per != prog.c_per || p_c)
		print_detail_progress_row((oper.num_block + 1), param.num_blocks, prog.c_dig_mat, prog.c_dig_wri, prog.c_dst_mat, prog.c_dst_wri);

	prog.p_dst_wri = prog.c_dst_wri;
	prog.p_dig_wri = prog.c_dig_wri;
	prog.p_dig_mat = prog.c_dig_mat;
	prog.p_dst_mat = prog.c_dst_mat;
	prog.p_per = prog.c_per;
}

void print_summary(void)
{
	if (flag.progress > 0)
		fprintf(flag.prst, "\n");

	fprintf(flag.prst, "%s: %zu/%zu blocks, %zu/%zu bytes.\n",
			flag.oper_mode == MAKEDIGEST ? (IS_MODE(digest.open_mode, READ) ? "Updated" : "Created") : (IS_MODE(dst.open_mode, READ) ? "Updated" : "Copied"),
			prog.wri_blocks, param.num_blocks, prog.wri_bytes, param.data_size);

	if (flag.oper_mode == BLOCKSYNC && oper.num_block + 1 < param.num_blocks) {
		fprintf(flag.prst, "Expected: %s, but was received: %s\n",
				format_units(param.data_size, true), format_units(src.abs_off, true));
		fprintf(stderr, "%s: The provided data is insufficient.\n", process_name);
	}
}

void parse_options(int argc, char **argv)
{
	static const char *short_options = "hVs:d:S:f:D:b:a:l";
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"progress", no_argument, &flag.progress, 1},
		{"show-progress", no_argument, &flag.progress, 1},
		{"progress-detail", no_argument, &flag.progress, 2},
		{"show-progress-detail", no_argument, &flag.progress, 2},
		{"silent", no_argument, &flag.silent, 1},
		{"force", no_argument, &flag.force, 1},
		{"mmap", no_argument, &flag.mmap, 1},
		{"no-compare", no_argument, &flag.no_compare, 1},
		{"sync-writes", no_argument, &flag.write_sync, 1},
		{"dont-write", no_argument, &flag.dont_write, 3},		 //(11)
		{"dont-write-target", no_argument, &flag.dont_write, 2}, //(10)
		{"dont-write-digest", no_argument, &flag.dont_write, 1}, //(01)
		{"src", required_argument, 0, 's'},
		{"dst", required_argument, 0, 'd'},
		{"size", required_argument,	0, 'S'},
		{"digest", required_argument, 0, 'f'},
		{"delta", required_argument, 0, 'D'},
		{"buffer-size", required_argument, 0, 1001},
		{"block-size", required_argument, 0, 'b'},
		{"algo", required_argument, 0, 'a'},
		{"list-algos", no_argument, 0, 'l'},
		{"benchmark-algos", no_argument, &flag.oper_mode, BENCHMARK},
		{"digest-info", no_argument, &flag.oper_mode, DIGESTINFO},
		{"delta-info", no_argument, &flag.oper_mode, DELTAINFO},
		{"make-delta", no_argument, &flag.oper_mode, MAKEDELTA},
		{"apply-delta", no_argument, &flag.oper_mode, APPLYDELTA},
		{"make-digest", no_argument, &flag.oper_mode, MAKEDIGEST},
		{0, 0, 0, 0}};

	int option_index;
	int c;

	while (1)
	{
		option_index = 0;
		c = getopt_long(argc, argv, short_options, long_options, &option_index);

		if (c == -1)
			break;

		switch (c)
		{
		case 's':
			src.path = optarg;
			break;
		case 'd':
			dst.path = optarg;
			break;
		case 'S':
			param.h_data_size = optarg;
			param.data_size = parse_units(optarg);
			break;
		case 'D':
			delta.path = optarg;
			break;
		case 'f':
			digest.path = optarg;
			break;
		case 'b':
			param.h_block_size = optarg;
			param.block_size = parse_units(optarg);
			break;
		case 'a':
			param.hash_algo = optarg;
			break;
		case 1001:
			param.max_buf_size = parse_units(optarg);
			break;
		case 'l':
			print_algos();
			exit(EXIT_SUCCESS);
			break;
		case 0:
			if (long_options[option_index].flag != 0)
				break;
			/*continue*/
		case 'h':
			print_help();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			print_version();
			exit(EXIT_SUCCESS);
			break;

		case 63: /*unrecognized option*/
			exit(EXIT_FAILURE);
			break;
		}
	}
}

void blocksync(void)
{
	size_t dev_flush = 0;
	size_t digest_flush = 0;
	bool dev_reload = false;
	bool digest_reload = false;

	while (src.abs_off < src.data_size)
	{
		if ((src.abs_off + src.block_size) > src.data_size)
			dst.block_size = src.block_size = src.data_size % src.block_size;

		dev_reload = check_buffer_reload(&src);
		digest_reload = check_buffer_reload(&digest);

		if (dev_flush > 0 || dev_reload)
			blocksync_dev_wri_flush(dev_flush);

		if (digest_flush > 0 || digest_reload)
			digest_wri_flush(digest_flush);

		if (dev_reload || digest_reload)
		{
			sync_data(&dst);
			sync_data(&digest);
		}

		if (dev_reload)
		{
			map_buffer(&src);
			map_buffer(&dst);
		}

		if (IS_MODE(digest.open_mode, WRITE) && digest_reload)
			map_buffer(&digest);

		get_ptr(&src);
		get_ptr(&dst);
		get_ptr(&digest);

		prog.c_dst_wri = true;
		prog.c_dst_mat = false;
		prog.c_dig_mat = false;

		if (IS_MODE(digest.open_mode, WRITE))
			prog.c_dig_wri = true;
		else
			prog.c_dig_wri = false;

		digest_flush = 0;
		dev_flush = 0;

		if (param.hash_use)
			hash_buffer(param.algo.value, param.algo.library, param.algo.size, (void *)(oper.hash_buf + (digest.rel_off - digest.mov_off)), src.ptr_r, src.block_size);

		if (IS_MODE(digest.open_mode, READ))
		{
			if (memcmp(digest.ptr_r, (const void *)(oper.hash_buf + (digest.rel_off - digest.mov_off)), param.algo.size) == 0)
			{
				prog.c_dst_wri = false;
				prog.c_dig_wri = false;
				prog.c_dig_mat = true;
			}
		}
		else if (IS_MODE(dst.open_mode, READ) && (memcmp(dst.ptr_r, src.ptr_r, src.block_size) == 0))
		{
			prog.c_dst_wri = false;
			prog.c_dst_mat = true;
		}

		if (prog.c_dst_wri)
		{
			prog.wri_blocks++;
			prog.wri_bytes += src.block_size;
			oper.dev_wri_buf_size += src.block_size;
		}
		else
			dev_flush = src.block_size;

		if (prog.c_dig_wri)
			oper.digest_wri_buf_size += digest.block_size;
		else
			digest_flush = digest.block_size;

		if (flag.progress > 1)
			print_detail_progress();
		else if (flag.progress == 1)
			print_progress();

		dst.abs_off = src.abs_off += src.block_size;
		dst.rel_off = src.rel_off += src.block_size;

		digest.abs_off += digest.block_size;
		digest.rel_off += digest.block_size;

		oper.num_block++;
	}

	blocksync_dev_wri_flush(0);
	digest_wri_flush(0);
}

void make_delta(void)
{
	size_t digest_flush = 0;
	bool dev_reload = false;
	bool digest_reload = false;
	bool delta_reload = false;

	while (src.abs_off < src.data_size)
	{
		if ((src.abs_off + src.block_size) > src.data_size)
		{
			src.block_size = src.data_size % src.block_size;
			delta.block_size = sizeof(u_int64_t) + src.block_size;
		}

		dev_reload = check_buffer_reload(&src);
		digest_reload = check_buffer_reload(&digest);
		delta_reload = check_buffer_reload(&delta);

		if (digest_flush > 0 || digest_reload)
			digest_wri_flush(digest_flush);

		if (delta_reload)
		{
			delta.data_size = delta.abs_off + delta.max_buf_size;
			dev_truncate(&delta);
			makedelta_wri_flush_buf();
			map_buffer(&delta);
		}

		if (digest_reload || delta_reload)
		{
			sync_data(&digest);
			sync_data(&delta);
		}

		if (dev_reload)
			map_buffer(&src);

		if (IS_MODE(digest.open_mode, WRITE) && digest_reload)
			map_buffer(&digest);

		get_ptr(&src);
		get_ptr(&digest);

		prog.c_dst_wri = true;
		prog.c_dst_mat = false;
		prog.c_dig_mat = false;
		if (IS_MODE(digest.open_mode, WRITE))
			prog.c_dig_wri = true;
		else
			prog.c_dig_wri = false;

		digest_flush = 0;

		if (param.hash_use)
			hash_buffer(param.algo.value, param.algo.library, param.algo.size, (void *)(oper.hash_buf + (digest.rel_off - digest.mov_off)), src.ptr_r, src.block_size);

		if (IS_MODE(digest.open_mode, READ))
		{
			if (memcmp(digest.ptr_r, (const void *)(oper.hash_buf + (digest.rel_off - digest.mov_off)), param.algo.size) == 0)
			{
				prog.c_dst_wri = false;
				prog.c_dig_wri = false;
				prog.c_dig_mat = true;
			}
		}

		if (prog.c_dst_wri)
		{
			prog.wri_blocks++;
			prog.wri_bytes += src.block_size;
			oper.dev_wri_buf_size += src.block_size;

			memcpy((void *)(oper.delta_buf + oper.delta_wri_buf_size), (uint64_t *)&src.abs_off, sizeof(uint64_t));
			memcpy((void *)(oper.delta_buf + oper.delta_wri_buf_size + sizeof(uint64_t)), (const void *)src.ptr_r, src.block_size);
			oper.delta_wri_buf_size += delta.block_size;

			delta.abs_off += delta.block_size;
			delta.rel_off += delta.block_size;
		}

		if (prog.c_dig_wri)
			oper.digest_wri_buf_size += digest.block_size;
		else
			digest_flush = digest.block_size;

		if (flag.progress > 1)
			print_detail_progress();
		else if (flag.progress == 1)
			print_progress();

		digest.abs_off += digest.block_size;
		digest.rel_off += digest.block_size;

		src.abs_off += src.block_size;
		src.rel_off += src.block_size;

		oper.num_block++;
	}

	digest_wri_flush(0);
	makedelta_wri_flush_buf();

	delta.data_size = delta.abs_off;
	dev_truncate(&delta);
}

void apply_delta(void)
{
	off_t cur_block_off = 0;
	off_t prev_block_off = 0;

	while (delta.abs_off < delta.data_size)
	{
		if (check_buffer_reload(&delta))
		{
			map_buffer(&delta);

			if (delta.abs_off == delta.data_size)
				break;
		}

		prev_block_off = cur_block_off;
		get_ptr(&delta);
		memcpy(&cur_block_off, delta.ptr_r, sizeof(uint64_t));

		if (cur_block_off - prev_block_off > delta.block_size)
		{
			prog.c_dst_wri = false;
			prog.c_dst_mat = true;
			oper.num_block = (prev_block_off) / param.block_size + 1;
			if (flag.progress > 1)
				print_detail_progress();
			else if (flag.progress == 1)
				print_progress();

			applydelta_wri_flush_buf(dst.block_size);
		}

		dst.abs_off += (cur_block_off - prev_block_off);
		dst.rel_off += (cur_block_off - prev_block_off);

		if (check_buffer_reload(&dst))
		{
			applydelta_wri_flush_buf(0);
			sync_data(&dst);
			map_buffer(&dst);
		}

		if ((delta.abs_off + delta.block_size) > delta.data_size)
		{
			delta.block_size = (delta.data_size - HEADER_SIZE) % delta.block_size;
			dst.block_size = delta.block_size - sizeof(u_int64_t);
		}

		memcpy((void *)(oper.delta_buf + oper.delta_wri_buf_size), (const void *)(delta.ptr_r + sizeof(uint64_t)), dst.block_size);
		oper.delta_wri_buf_size += dst.block_size;

		prog.wri_blocks++;
		prog.wri_bytes += dst.block_size;

		prog.c_dst_wri = true;
		prog.c_dst_mat = false;
		oper.num_block = (cur_block_off) / param.block_size;

		if (flag.progress > 1)
			print_detail_progress();
		else if (flag.progress == 1)
			print_progress();

		delta.abs_off += delta.block_size;
		delta.rel_off += delta.block_size;
	}

	prog.c_dst_wri = false;
	prog.c_dst_mat = true;

	if (oper.num_block < param.num_blocks - 2)
	{
		oper.num_block++;

		if (flag.progress > 1)
			print_detail_progress();
		else if (flag.progress == 1)
			print_progress();
	}

	if (oper.num_block < param.num_blocks - 1)
	{
		oper.num_block = param.num_blocks - 1;

		if (flag.progress > 1)
			print_detail_progress();
		else if (flag.progress == 1)
			print_progress();
	}

	applydelta_wri_flush_buf(dst.block_size);
}

void make_digest(void)
{
	size_t digest_flush = 0;
	bool dev_reload = false;
	bool digest_reload = false;

	while (src.abs_off < src.data_size)
	{
		if ((src.abs_off + src.block_size) > src.data_size)
			src.block_size = src.data_size % src.block_size;

		dev_reload = check_buffer_reload(&src);
		digest_reload = check_buffer_reload(&digest);

		if (digest_flush > 0 || digest_reload)
			digest_wri_flush(digest_flush);

		if (digest_reload)
			sync_data(&digest);

		if (dev_reload)
			map_buffer(&src);

		if (digest_reload)
			map_buffer(&digest);

		get_ptr(&src);
		get_ptr(&digest);

		prog.c_dig_mat = false;
		prog.c_dig_wri = true;

		digest_flush = 0;

		if (param.hash_use)
			hash_buffer(param.algo.value, param.algo.library, param.algo.size, (void *)(oper.hash_buf + (digest.rel_off - digest.mov_off)), src.ptr_r, src.block_size);

		if (IS_MODE(digest.open_mode, READ))
		{
			if (memcmp(digest.ptr_r, (const void *)(oper.hash_buf + (digest.rel_off - digest.mov_off)), param.algo.size) == 0)
			{
				prog.c_dig_wri = false;
				prog.c_dig_mat = true;
			}
		}

		if (prog.c_dig_wri)
		{
			oper.digest_wri_buf_size += digest.block_size;
			prog.wri_blocks++;
			prog.wri_bytes += digest.block_size;
		}
		else
			digest_flush = digest.block_size;

		if (flag.progress > 1)
			print_detail_progress();
		else if (flag.progress == 1)
			print_progress();

		digest.abs_off += digest.block_size;
		digest.rel_off += digest.block_size;

		src.abs_off += src.block_size;
		src.rel_off += src.block_size;

		oper.num_block++;
	}

	digest_wri_flush(0);
}

void init_params(void)
{
	if (flag.oper_mode == MAKEDELTA && delta.path == NULL)
		flag.prst = stderr;

	if (flag.silent)
		freopen("/dev/null", "w", flag.prst) != NULL;

	init_map_methods();

	if (flag.oper_mode == BLOCKSYNC)
	{
		fprintf(flag.prst, "Operation mode: block-sync\n");

		if (src.path == NULL || dst.path == NULL)
		{
			if (src.path == NULL)
				fprintf(stderr, "%s - you need to specify the source path (-s, --src=PATH)\n", process_name);

			if (dst.path == NULL)
				fprintf(stderr, "%s - you need to specify the target path (-d, --dst=PATH)\n", process_name);

			fprintf(flag.prst, "Try '%s --help' for more information.\n", process_name);
			cleanup(EXIT_FAILURE);
		}

		if (param.hash_algo != NULL)
			check_algo_param();

		check_block_size();
		init_src_device();
		init_dst_device();

		if (digest.path != NULL)
			init_digest_file();
		else
			fprintf(flag.prst, "Warning: works without digest file.\n", process_name);

		if (IS_MODE(digest.open_mode, READ))
			dst.open_mode ^= READ;

		if (!IS_MODE(digest.open_mode, READ) && IS_MODE(dst.open_mode, READ))
			fprintf(flag.prst, "Works without reads from digest file, data to compare will be read from the destination device\n");

		else if (!IS_MODE(digest.open_mode, WRITE))
			fprintf(flag.prst, "Works without writes to digest file, checksums won't be saved\n");

		else if (IS_MODE(digest.open_mode, READ) && IS_MODE(dst.open_mode, READ))
			fprintf(flag.prst, "Works with digest file, checksums will be readed and updated\n");

		else
			fprintf(flag.prst, "Works with digest file, checksums will be saved\n");

		if (flag.no_compare == 1)
			fprintf(flag.prst, "Warning: copying all data without comparing differences\n");
	}

	if (flag.oper_mode == MAKEDELTA)
	{
		fprintf(flag.prst, "Operation mode: make-delta\n");

		if (src.path == NULL)
		{

			if (src.path == NULL)
				fprintf(stderr, "%s - you need to specify the source path (-s, --src=PATH)\n", process_name);

			fprintf(flag.prst, "Try '%s --help' for more information.\n", process_name);
			cleanup(EXIT_FAILURE);
		}

		if (digest.path == NULL)
			fprintf(flag.prst, "Warning: works without digest file.\n", process_name);

		if (param.hash_algo != NULL)
			check_algo_param();

		init_src_device();
		if (digest.path != NULL)
			init_digest_file();
		init_dst_delta();
	}

	if (flag.oper_mode == APPLYDELTA)
	{
		fprintf(flag.prst, "Operation mode: apply-delta\n");

		if (dst.path == NULL)
		{
			fprintf(stderr, "%s - you need to specify the target path (-d, --dst=PATH)\n", process_name);

			fprintf(flag.prst, "Try '%s --help' for more information.\n", process_name);
			cleanup(EXIT_FAILURE);
		}

		init_src_delta();
		init_dst_device();
	}

	if (flag.oper_mode == MAKEDIGEST)
	{
		fprintf(flag.prst, "Operation mode: make-digest\n");

		if (src.path == NULL || digest.path == NULL)
		{

			if (src.path == NULL)
				fprintf(stderr, "%s - you need to specify the source path (-s, --src=PATH)\n", process_name);

			if (dst.path == NULL)
				fprintf(stderr, "%s - you need to specify the digest path (-f, --digest=PATH)\n", process_name);

			fprintf(flag.prst, "Try '%s --help' for more information.\n", process_name);
			cleanup(EXIT_FAILURE);
		}

		if (param.hash_algo != NULL)
			check_algo_param();

		check_block_size();
		init_src_device();
		init_digest_file();
		param.data_size = digest.data_size - HEADER_SIZE;
	}

	fprintf(flag.prst, "Buffer size: %s per device\n", format_units(param.max_buf_size, true));

	if (flag.oper_mode == BLOCKSYNC || flag.oper_mode == MAKEDELTA || flag.oper_mode == MAKEDIGEST)
	{
		src.max_buf_size = param.max_buf_size;
		src.block_size = param.block_size;

		if (adjust_buffer(&src.max_buf_size, src.block_size))
			fprintf(flag.prst, "Adjusting buffer size to: %s for src device\n", format_units(src.max_buf_size, true));

		src.buf_size = (src.data_size < src.max_buf_size ? src.data_size : src.max_buf_size);
	}

	if (flag.oper_mode == BLOCKSYNC || flag.oper_mode == APPLYDELTA)
	{
		dst.max_buf_size = param.max_buf_size;
		dst.block_size = param.block_size;

		if (adjust_buffer(&dst.max_buf_size, dst.block_size))
			fprintf(flag.prst, "Adjusting buffer size to: %s for dst device\n", format_units(dst.max_buf_size, true));

		dst.buf_size = (dst.data_size < dst.max_buf_size ? dst.data_size : dst.max_buf_size);
	}

	bool buf_adj_delta = false;

	if (flag.oper_mode == MAKEDELTA)
	{
		delta.block_size = sizeof(u_int64_t) + param.block_size;
		delta.max_buf_size = (src.max_buf_size / param.block_size) * delta.block_size;
		// buf_adj_delta = adjust_buffer(&delta.max_buf_size, delta.block_size);

		if (IS_MODE(delta.open_mode, DIRECT_W) || IS_MODE(delta.open_mode, PIPE_W))
			delta.buf_data = realloc(delta.buf_data, delta.max_buf_size);

		oper.delta_buf = malloc(delta.max_buf_size);
		map_buffer(&delta);
	}

	if (flag.oper_mode == APPLYDELTA)
	{
		delta.block_size = sizeof(u_int64_t) + param.block_size;
		delta.max_buf_size = (dst.max_buf_size / param.block_size) * delta.block_size;
		// buf_adj_delta = adjust_buffer(&delta.max_buf_size, delta.block_size);

		if (IS_MODE(delta.open_mode, DIRECT_R) || IS_MODE(delta.open_mode, PIPE_R))
			delta.buf_data = realloc(delta.buf_data, delta.max_buf_size);

		oper.delta_buf = malloc(dst.max_buf_size);
		map_buffer(&delta);
	}

	if (flag.oper_mode == BLOCKSYNC || flag.oper_mode == MAKEDELTA || flag.oper_mode == MAKEDIGEST)
	{
		if (IS_MODE(src.open_mode, DIRECT) || IS_MODE(src.open_mode, PIPE))
			src.buf_data = malloc(src.buf_size);

		if (IS_MODE(digest.open_mode, WRITE))
		{
			digest.block_size = param.algo.size;
			digest.max_buf_size = (src.max_buf_size / src.block_size) * digest.block_size;
			bool buf_adj_digest = adjust_buffer(&digest.max_buf_size, digest.block_size);

			if (IS_MODE(digest.open_mode, DIRECT))
				digest.buf_data = realloc(digest.buf_data, digest.max_buf_size);

			map_buffer(&digest);
		}

		if (param.block_size < src.stat.st_blksize)
			fprintf(flag.prst, "Warning: given block size is smaller than the block size of the source device, which is %zu bytes\n",
					src.stat.st_blksize);
	}

	if (flag.oper_mode == BLOCKSYNC || flag.oper_mode == APPLYDELTA)
	{
		if (IS_MODE(dst.open_mode, DIRECT))
			dst.buf_data = malloc(dst.buf_size);
	}

	fprintf(flag.prst, "Block size: %s per block out of %zu blocks\n", format_units(param.block_size, true), param.num_blocks);

	if (param.hash_use)
	{
		hash_init(param.algo.value, param.algo.library);
		oper.hash_buf = (char *)hash_alloc(digest.buf_size, param.algo.library);

		fprintf(flag.prst, "Hash algo: '%s' uses %d bytes per block\n",
				param.algo.symbol, param.algo.size);

		if (param.algo.size > param.block_size)
			fprintf(flag.prst, "Warning: block size '%ld' is smaller than hash '%s' size\n", param.block_size, param.algo.symbol);
	}

	if (param.data_size > (size_t)(1UL * 1024 * 1024 * 1024 * 1024)) {
		param.pro_prec = 2;
		param.pro_fact = 100;
	}
	else if (param.data_size > (size_t)(100UL * 1024 * 1024 * 1024)) {
		param.pro_prec = 1;
		param.pro_fact = 10;
	}

	snprintf(param.pro_form, sizeof(param.pro_form), "\rProgress: %%.%df%s", param.pro_prec, "%%");
}

int main(int argc, char **argv)
{
	PAGE_SIZE = getpagesize();
	flag.prst = stdout;

	process_name = basename(argv[0]);
	parse_options(argc, argv);

	switch (flag.oper_mode)
	{
	case BENCHMARK:
		#include "benchmark.h"
		benchmark_hashes();
		break;

	case DIGESTINFO:
		#include "digest_info.h"
		digest_info();
		break;

	case DELTAINFO:
		#include "digest_info.h"
		delta_info();
		break;

	case BLOCKSYNC:
		init_params();
		blocksync();
		print_summary();
		break;

	case MAKEDELTA:
		init_params();
		make_delta();
		print_summary();
		break;

	case APPLYDELTA:
		init_params();
		apply_delta();
		print_summary();
		break;

	case MAKEDIGEST:
		init_params();
		make_digest();
		print_summary();
		break;
	}

	cleanup(EXIT_SUCCESS);
}
