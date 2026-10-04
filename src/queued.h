#ifndef BSF_QUEUED_H
#define BSF_QUEUED_H

/* Zero jobs selects the existing, synchronous path. */
extern unsigned read_jobs, read_depth;
extern size_t read_size;
extern int direct_read, read_options, destination_resized, digest_recovery;
size_t read_option_number(const char *text, int units);
void validate_read_options(void);
int queued_sync(void);
void sync_guard_begin(void);
void sync_guard_finish(void);
void sync_guard_check(void);

#endif
