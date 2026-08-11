#ifndef PROJECT_HEADER_H
#define PROJECT_HEADER_H

#include <stddef.h>

void sanitize_stage_name(char *out, size_t outsz, const char *name);
void sync_bdb_header_counts(void);

#endif
