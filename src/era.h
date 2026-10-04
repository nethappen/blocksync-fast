#ifndef BSF_ERA_H
#define BSF_ERA_H

void era_load(void);
void era_check_source(void);
void era_require_digest(void);
off_t era_next_offset(off_t offset);
void era_finish(void);
void era_free(void);
void era_set_sectors(const char *value);

#endif
