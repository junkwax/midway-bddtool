#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <stddef.h>

void path_join(char *out, size_t outsz, const char *dir, const char *file);
bool stage_file_exists(const char *path);
void stage_dirname(const char *path, char *out, size_t outsz);
bool ensure_dir_recursive(const char *path);
int run_command_capture(const char *cmd, char *out, size_t outsz);

#endif
