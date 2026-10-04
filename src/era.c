/* DM-era range filtering, based on Sven-Ola Tuecke's --era implementation.
 * SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "globals.h"
#include "era.h"
#include "queued.h"
#include <ctype.h>
#include <limits.h>
#include <sys/file.h>

struct era_range { uint64_t begin, end; };
static struct era_range *ranges;
static size_t range_count, range_capacity, cursor;
static char *xml, *position, *marker;
static int marker_fd = -1;
static int sectors_given;

static void bad(const char *reason)
{
    fprintf(stderr, "%s: --era: %s\n", process_name, reason);
    cleanup(EXIT_FAILURE);
}

void era_set_sectors(const char *value)
{
    char *end;
    errno = 0;
    unsigned long long n = strtoull(value, &end, 10);
    if (!isdigit((unsigned char)*value) || *end || errno || !n ||
        n > MIN((uint64_t)SIZE_MAX, (uint64_t)LLONG_MAX) / 512)
        bad("--era-sectors must be a positive sector count without overflow");
    param.era_sectors = n;
    sectors_given = 1;
}

static void whitespace(void)
{
    while (isspace((unsigned char)*position)) position++;
}

static int take(const char *text)
{
    size_t length = strlen(text);
    if (strncmp(position, text, length)) return 0;
    position += length;
    return 1;
}

static void skip_comments(void)
{
    whitespace();
    while (take("<!--")) {
        char *end = strstr(position, "-->");
        if (!end) bad("unterminated XML comment");
        position = end + 3;
        whitespace();
    }
}

/* Parse the small era_invalidate schema, without entities, DTDs or extensions.
 * Numeric attributes are strictly unsigned; attribute order is immaterial. */
static uint64_t number(void)
{
    whitespace();
    if (!take("=")) bad("expected '=' in range attribute");
    whitespace();
    char quote = *position++;
    if (quote != '\'' && quote != '"') bad("expected quoted range index");
    if (!isdigit((unsigned char)*position)) bad("range index must be unsigned");
    char *end;
    errno = 0;
    uint64_t n = strtoull(position, &end, 10);
    if (errno || *end != quote) bad("invalid or overflowing range index");
    position = end + 1;
    return n;
}

static void add_range(uint64_t begin, uint64_t end)
{
    uint64_t bytes = (uint64_t)param.era_sectors * 512;
    uint64_t limit = MIN((uint64_t)SIZE_MAX, (uint64_t)LLONG_MAX);
    if (begin >= end || end > limit / bytes) bad("empty, reversed or overflowing range");
    if (range_count == range_capacity) {
        size_t next = range_capacity ? range_capacity * 2 : 64;
        struct era_range *grown = realloc(ranges, next * sizeof(*ranges));
        if (!grown) bad("cannot allocate range list");
        ranges = grown;
        range_capacity = next;
    }
    ranges[range_count++] = (struct era_range){begin * bytes, end * bytes};
}

static int order(const void *a, const void *b)
{
    const struct era_range *x = a, *y = b;
    return x->begin < y->begin ? -1 : x->begin > y->begin;
}

void era_load(void)
{
    if (!param.era) {
        if (sectors_given) bad("--era-sectors requires --era");
        return;
    }
    if ((flag.oper_mode != MAKEDIGEST && flag.oper_mode != MAKEDELTA) || read_options)
        bad("supported only with --make-digest or --make-delta, without queued reads");
    if (!src.path || !strcmp(src.path, "-") || !digest.path || !strcmp(digest.path, "-"))
        bad("requires a seekable source and an existing named baseline digest");
    if (flag.no_compare || (flag.oper_mode == MAKEDELTA && flag.dont_write))
        bad("--no-compare and make-delta write-suppression options are unsupported");
    /* Validate all output aliases before opening any output for writing. */
    const char *paths[] = {src.path, digest.path, delta.path, strcmp(param.era, "-") ? param.era : NULL};
    struct stat stats[4];
    int exists[4] = {0};
    for (size_t i = 0; i < 4; i++) {
        exists[i] = paths[i] && !stat(paths[i], &stats[i]);
        for (size_t j = 0; j < i; j++)
            if (exists[i] && exists[j] &&
                ((stats[i].st_dev == stats[j].st_dev && stats[i].st_ino == stats[j].st_ino) ||
                 (S_ISBLK(stats[i].st_mode) && S_ISBLK(stats[j].st_mode) && stats[i].st_rdev == stats[j].st_rdev)))
                bad("source, digest, delta and XML must not alias");
    }
    if (!exists[1] || !S_ISREG(stats[1].st_mode)) bad("baseline digest must be an existing regular file");
    FILE *input = !strcmp(param.era, "-") ? stdin : fopen(param.era, "r");
    if (!input) bad("cannot open change list");
    /* Bound parser memory and reject binary/truncated XML before mutations. */
    const size_t limit = 16 * 1024 * 1024;
    xml = malloc(limit + 1);
    if (!xml) bad("cannot allocate XML input");
    size_t length = fread(xml, 1, limit, input);
    if (ferror(input) || (length == limit && fgetc(input) != EOF)) bad("read error or XML exceeds 16 MiB");
    if (input != stdin) fclose(input);
    if (memchr(xml, 0, length)) bad("embedded NUL in XML");
    xml[length] = 0;
    position = xml;
    whitespace();
    if (take("<?xml")) {
        char *end = strstr(position, "?>");
        if (!end) bad("unterminated XML declaration");
        position = end + 2;
    }
    skip_comments();
    if (!take("<blocks")) bad("expected <blocks> root");
    whitespace();
    int closed = take("/>");
    if (!closed && !take(">")) bad("invalid root element");
    while (!closed) {
        skip_comments();
        if (take("</blocks")) {
            whitespace();
            if (!take(">")) bad("invalid closing root");
            closed = 1;
            break;
        }
        int single;
        if (take("<block") && isspace((unsigned char)*position)) single = 1;
        else if (take("<range") && isspace((unsigned char)*position)) single = 0;
        else bad("expected a self-closing <block> or <range>");
        uint64_t begin = 0, end = 0;
        unsigned attributes = 0;
        for (;;) {
            whitespace();
            if (take("/>")) break;
            if ((single && take("block")) || (!single && take("begin"))) {
                if (attributes & 1) bad("duplicate begin/block attribute");
                begin = number(); attributes |= 1;
            } else if (!single && take("end")) {
                if (attributes & 2) bad("duplicate end attribute");
                end = number(); attributes |= 2;
            } else bad("unknown range attribute");
            if (!isspace((unsigned char)*position) && strncmp(position, "/>", 2))
                bad("expected whitespace between attributes");
        }
        if (attributes != (single ? 1U : 3U)) bad("missing range attribute");
        if (single) {
            if (begin == UINT64_MAX) bad("overflowing block index");
            end = begin + 1;
        }
        add_range(begin, end);
    }
    skip_comments();
    if (*position) bad("unexpected content after </blocks>");
    free(xml); xml = NULL;
    if (range_count) qsort(ranges, range_count, sizeof(*ranges), order);
    size_t count = 0;
    for (size_t i = 0; i < range_count; i++) {
        if (count && ranges[i].begin <= ranges[count-1].end)
            ranges[count-1].end = MAX(ranges[count-1].end, ranges[i].end);
        else ranges[count++] = ranges[i];
    }
    range_count = count;
}

void era_check_source(void)
{
    if (!param.era) return;
    uint64_t bytes = (uint64_t)param.era_sectors * 512;
    for (size_t i = 0; i < range_count; i++)
        if (!src.data_size || ranges[i].begin >= src.data_size ||
            (ranges[i].end - 1) / bytes > (src.data_size - 1) / bytes)
            bad("range lies outside source capacity");
}

static int sync_directory(const char *path)
{
    char *copy = strdup(path);
    if (!copy) return -1;
    int fd = open(dirname(copy), O_RDONLY | O_DIRECTORY);
    free(copy);
    if (fd < 0) return -1;
    int result = fsync(fd);
    close(fd);
    return result;
}

void era_require_digest(void)
{
    if (!param.era) return;
    if (!IS_MODE(digest.open_mode, READ)) bad("requires a complete, compatible baseline digest; generate a full digest first");
    if (flock(digest.fd, LOCK_EX | LOCK_NB)) bad("baseline digest is locked by another operation");
    if (BIT_SET(flag.dont_write, 0)) return;
    if (asprintf(&marker, "%s.incomplete", digest.path) < 0) bad("cannot allocate marker path");
    marker_fd = open(marker, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (marker_fd < 0 || flock(marker_fd, LOCK_EX | LOCK_NB)) bad("cannot create incomplete marker; recover baseline first");
    const char message[] = "Incomplete DM-era digest update. Rebuild a full baseline before retry.\n";
    if (write_at_all(marker_fd, message, sizeof(message)-1, 0) < 0 || fsync(marker_fd) || sync_directory(marker))
        bad("cannot persist incomplete marker");
}

/* Expand selected byte intervals to whole hash blocks and jump over gaps. */
off_t era_next_offset(off_t offset)
{
    if (!param.era) return offset;
    while (cursor < range_count && ranges[cursor].end <= (uint64_t)offset) cursor++;
    if (cursor == range_count) return src.data_size;
    off_t begin = ranges[cursor].begin / param.block_size * param.block_size;
    return MAX(offset, begin);
}

void era_finish(void)
{
    if (!param.era) return;
    struct stat after;
    if (fstat(src.fd, &after) || (S_ISREG(after.st_mode) && after.st_size != src.stat.st_size))
        bad("source size changed during operation");
    if (!marker) return;
    if (fsync(digest.fd) || (flag.oper_mode == MAKEDELTA &&
        ((delta.path && fsync(delta.fd)) || (delta.path && sync_directory(delta.path)))) ||
        sync_directory(digest.path) || unlink(marker) || sync_directory(marker))
        bad("cannot synchronize outputs or remove incomplete marker");
    close(marker_fd); marker_fd = -1;
}

void era_free(void)
{
    free(ranges);
    free(xml);
    free(marker);
    if (marker_fd >= 0) close(marker_fd);
}
