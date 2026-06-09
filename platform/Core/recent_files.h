#ifndef RECENT_FILES_H
#define RECENT_FILES_H

#ifdef __cplusplus
extern "C" {
#endif

extern char g_recent_files[8][512];
extern int g_recent_count;

void recent_add(const char *path);
void recent_save(void);
void recent_load(void);

#ifdef __cplusplus
}
#endif

#endif
