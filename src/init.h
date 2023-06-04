/*
 ./src/init.h - this file is a part of program blocksync-fast

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

#ifndef INIT_H
#define INIT_H

void init_map_methods(void);
void check_algo_param(void);
void check_block_size(void);
void init_src_device(void);
void init_dst_device(void);
void init_digest_file(void);
void init_dst_delta(void);
void init_src_delta(void);
#endif
