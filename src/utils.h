/*
 ./src/utils.h - this file is a part of program blocksync-fast

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

#ifndef UTILS_H
#define UTILS_H

off_t parse_units(char *size);
char *format_units(off_t size, bool show_bytes);
off_t p2r(off_t x);

#endif
