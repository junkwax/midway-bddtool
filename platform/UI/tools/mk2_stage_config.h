#ifndef MK2_STAGE_CONFIG_H
#define MK2_STAGE_CONFIG_H

#include <stddef.h>
#include <stdio.h>

void stage_append(char *out, size_t outsz, const char *text);
void stage_append_arg(char *out, size_t outsz, const char *arg);
void json_write_string(FILE *f, const char *s);
bool stage_write_config(void);
void stage_build_command(const char *action, char *out, size_t outsz);

#endif
