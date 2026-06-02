#include "bg_editor_globals.h"
#include <cstdio>
#include <cstring>

char g_recent_files[8][512];
int  g_recent_count = 0;

void recent_add(const char *path)
{
    if (!path || !path[0]) return;

    for (int i = 0; i < g_recent_count; i++) {
        if (strcmp(g_recent_files[i], path) == 0) {
            char tmp[512];
            memcpy(tmp, g_recent_files[i], sizeof tmp);
            for (int j = i; j > 0; j--)
                memcpy(g_recent_files[j], g_recent_files[j - 1], sizeof g_recent_files[0]);
            memcpy(g_recent_files[0], tmp, sizeof g_recent_files[0]);
            return;
        }
    }

    if (g_recent_count < 8) g_recent_count++;
    for (int j = g_recent_count - 1; j > 0; j--)
        memcpy(g_recent_files[j], g_recent_files[j - 1], sizeof g_recent_files[0]);
    snprintf(g_recent_files[0], sizeof g_recent_files[0], "%s", path);
}

void recent_save(void)
{
    FILE *f = fopen("bddview_recent.txt", "w");
    if (!f) return;
    for (int i = 0; i < g_recent_count; i++)
        fprintf(f, "%s\n", g_recent_files[i]);
    fclose(f);
}

void recent_load(void)
{
    FILE *f = fopen("bddview_recent.txt", "r");
    if (!f) return;
    g_recent_count = 0;
    char line[512];
    while (fgets(line, sizeof line, f) && g_recent_count < 8) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) snprintf(g_recent_files[g_recent_count++], 512, "%s", line);
    }
    fclose(f);
}
