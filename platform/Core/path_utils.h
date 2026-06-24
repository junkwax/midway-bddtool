#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <stddef.h>

void path_join(char *out, size_t outsz, const char *dir, const char *file);
bool stage_file_exists(const char *path);
void stage_dirname(const char *path, char *out, size_t outsz);
bool ensure_dir_recursive(const char *path);
bool bddtool_backup_dir(char *out, size_t outsz, const char *subdir);
bool bddtool_backup_path(char *out, size_t outsz, const char *src_path,
                         const char *suffix, const char *subdir);
int run_command_capture(const char *cmd, char *out, size_t outsz);

#endif
