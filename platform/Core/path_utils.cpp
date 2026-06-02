#include "bg_editor_globals.h"

#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

void path_join(char *out, size_t outsz, const char *dir, const char *file)
{
    if (!out || outsz == 0) return;
    if (!dir || !dir[0]) {
        snprintf(out, outsz, "%s", file ? file : "");
        return;
    }
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    size_t len = strlen(dir);
    if (dir[len - 1] == '\\' || dir[len - 1] == '/')
        snprintf(out, outsz, "%s%s", dir, file ? file : "");
    else
        snprintf(out, outsz, "%s%c%s", dir, sep, file ? file : "");
}

bool stage_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void stage_dirname(const char *path, char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    snprintf(out, outsz, "%s", path ? path : "");
    char *sep = strrchr(out, '\\');
    char *sep2 = strrchr(out, '/');
    if (!sep || (sep2 && sep2 > sep)) sep = sep2;
    if (sep) *sep = '\0';
    else snprintf(out, outsz, ".");
}

void resolve_stage_file(char *out, size_t outsz, const char *path)
{
    if (!path || !path[0]) {
        if (outsz) out[0] = '\0';
        return;
    }
    if ((strlen(path) > 2 && path[1] == ':') || path[0] == '\\' || path[0] == '/')
        snprintf(out, outsz, "%s", path);
    else
        path_join(out, outsz, g_stage_dir, path);
}

bool ensure_dir_recursive(const char *path)
{
    if (!path || !path[0]) return false;
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        if (p == tmp) continue;
        if (p == tmp + 2 && tmp[1] == ':') continue;
        char saved = *p;
        *p = '\0';
        if (tmp[0]) {
#ifdef _WIN32
            CreateDirectoryA(tmp, NULL);
#else
            mkdir(tmp, 0755);
#endif
        }
        *p = saved;
    }
#ifdef _WIN32
    CreateDirectoryA(tmp, NULL);
    DWORD attr = GetFileAttributesA(tmp);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    mkdir(tmp, 0755);
    struct stat st;
    return stat(tmp, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}
