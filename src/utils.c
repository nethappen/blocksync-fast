/*
 ./src/utils.c - this file is a part of program blocksync-fast

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

long parse_units(char *size)
{
    long number;
    char *str;

    number = strtoul(size, &str, 10);

    if (strcasecmp(str, "k") == 0 || strcasecmp(str, "kib") == 0)
        number *= 1024;
    else if (strcasecmp(str, "kb") == 0)
        number *= 1000;
    else if (strcasecmp(str, "m") == 0 || strcasecmp(str, "mib") == 0)
        number *= 1024 * 1024;
    else if (strcasecmp(str, "mb") == 0)
        number *= 1000 * 1000;
    else if (strcasecmp(str, "g") == 0 || strcasecmp(str, "gib") == 0)
        number *= 1024 * 1024 * 1024;
    else if (strcasecmp(str, "gb") == 0)
        number *= 1000 * 1000 * 1000;

    return number;
}

char *format_units(long long int size, bool show_bytes)
{
    char *number_str = malloc(80);

    if (size >= 1099511627776)
        sprintf(number_str, ("%.2f TiB"), ((double)size / 1099511627776));
    else if (size >= 1073741824)
        sprintf(number_str, ("%.2f GiB"), ((double)size / 1073741824));
    else if (size >= 1048576)
        sprintf(number_str, ("%.2f MiB"), ((double)size / 1048576));
    else if (size >= 1024)
        sprintf(number_str, ("%.2f KiB"), ((double)size / 1024));
    else
    {
        if (show_bytes)
            sprintf(number_str, ("%llu bytes"), size);
        else
            sprintf(number_str, ("%.0f B"), (double)size);

        show_bytes = false;
    }

    if (show_bytes)
    {
        char *number_str2 = malloc(80);
        memcpy(number_str2, number_str, 80);
        sprintf(number_str, ("%s, %llu bytes"), number_str2, size);
    }

    return number_str;
}

off_t p2r(off_t x)
{
    if (x < 0)
        return 0;
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return x + 1;
}
