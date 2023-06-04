/*
 ./src/benchmark.c - this file is a part of program blocksync-fast

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

void benchmark_hashes(void)
{
	if (param.block_size < 4 || param.block_size > 4 * 1024 * 1024)
	{
		fprintf(stderr, "%s: the block size for the test should be between 4 bytes and 4M bytes\n", process_name);
		cleanup(EXIT_FAILURE);
	}

	fprintf(flag.prst, "Block size: %zu bytes\n", param.block_size);
	fprintf(flag.prst, "Filling buffer with random data ...\n");

	char random_data[param.block_size];
	if (getrandom(random_data, param.block_size, 0) < 1)
	{
		fprintf(stderr, "%s: cant get random data\n", process_name);
		abort();
	}

	int j = 0;

	for (int i = -1; i <= 1024; i++)
	{
		if (algos[MAX(i, j)].value == 0)
			break;

		param.algo = algos[MAX(i, j)];

		hash_init(param.algo.value, param.algo.library);
		char *hash_buf = (char *)hash_alloc(param.block_size, param.algo.library);

		time_t start_time = time(NULL);
		int count = 0;
		while (time(NULL) - start_time < 1)
		{
			hash_buffer(param.algo.value, param.algo.library, param.algo.size, (void *)(hash_buf), (const void *)random_data, param.block_size);
			count++;
		}

		hash_free(param.algo.value, param.algo.library, hash_buf);

		if (i >= 0)
			fprintf(flag.prst, "Algo: %-15s\tHash size: %3d bytes\t\tSpeed:%10d hashes/s\tProcessing: %10s/s\n", param.algo.symbol, param.algo.size, count, format_units(param.block_size * count, false));
	}
}