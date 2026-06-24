#include <cstdio>
#include <cstring>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
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

static const char *path_local_basename(const char *path)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++) {
        if (*p == '\\' || *p == '/')
            base = p + 1;
    }
    return base;
}

static unsigned int path_stable_hash(const char *path)
{
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)(path ? path : "");
    while (*p) {
        unsigned char c = *p++;
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        h ^= c;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static void path_dirname_copy(const char *path, char *out, size_t outsz)
{
    char tmp[640];
    if (!out || outsz == 0) return;
    snprintf(tmp, sizeof tmp, "%s", path ? path : "");
    snprintf(out, outsz, "%s", tmp);
    char *sep = strrchr(out, '\\');
    char *sep2 = strrchr(out, '/');
    if (!sep || (sep2 && sep2 > sep)) sep = sep2;
    if (!sep) {
        snprintf(out, outsz, ".");
        return;
    }
    if (sep > out && (sep[1] != '\0'))
        *sep = '\0';
}

static bool path_parent_inplace(char *path)
{
    if (!path || !path[0]) return false;
    size_t n = strlen(path);
    while (n > 1 && (path[n - 1] == '\\' || path[n - 1] == '/'))
        path[--n] = '\0';
    char *sep = strrchr(path, '\\');
    char *sep2 = strrchr(path, '/');
    if (!sep || (sep2 && sep2 > sep)) sep = sep2;
    if (!sep || sep == path) return false;
    if (sep == path + 2 && path[1] == ':') {
        sep[1] = '\0';
        return false;
    }
    *sep = '\0';
    return path[0] != '\0';
}

static bool path_dir_exists(const char *path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool bddtool_root_markers(const char *dir)
{
    char cmake[640];
    char platform_dir[640];
    if (!dir || !dir[0]) return false;
    path_join(cmake, sizeof cmake, dir, "CMakeLists.txt");
    path_join(platform_dir, sizeof platform_dir, dir, "platform");
    return stage_file_exists(cmake) && path_dir_exists(platform_dir);
}

static bool bddtool_find_root_from(char *dir)
{
    for (int i = 0; i < 10 && dir && dir[0]; i++) {
        if (bddtool_root_markers(dir))
            return true;
        if (!path_parent_inplace(dir))
            break;
    }
    return false;
}

static bool bddtool_root_dir(char *out, size_t outsz)
{
    char probe[640];
    char fallback[640] = ".";
    if (!out || outsz == 0) return false;

#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, probe, (DWORD)sizeof probe);
    if (n > 0 && n < sizeof probe) {
        probe[n] = '\0';
        path_dirname_copy(probe, probe, sizeof probe);
        snprintf(fallback, sizeof fallback, "%s", probe);
        if (bddtool_find_root_from(probe)) {
            snprintf(out, outsz, "%s", probe);
            return true;
        }
    }
    n = GetCurrentDirectoryA((DWORD)sizeof probe, probe);
    if (n > 0 && n < sizeof probe && bddtool_find_root_from(probe)) {
        snprintf(out, outsz, "%s", probe);
        return true;
    }
#else
    if (getcwd(probe, sizeof probe) && bddtool_find_root_from(probe)) {
        snprintf(out, outsz, "%s", probe);
        return true;
    }
#endif
    snprintf(out, outsz, "%s", fallback);
    return false;
}

bool bddtool_backup_dir(char *out, size_t outsz, const char *subdir)
{
    char root[512];
    char backup_root[640];
    if (!out || outsz == 0) return false;
    bddtool_root_dir(root, sizeof root);
    path_join(backup_root, sizeof backup_root, root, "backups");
    if (subdir && subdir[0])
        path_join(out, outsz, backup_root, subdir);
    else
        snprintf(out, outsz, "%s", backup_root);
    return ensure_dir_recursive(out);
}

bool bddtool_backup_path(char *out, size_t outsz, const char *src_path,
                         const char *suffix, const char *subdir)
{
    char dir[512];
    char name[256];
    const char *base;
    unsigned int hash;

    if (!out || outsz == 0) return false;
    out[0] = '\0';
    if (!bddtool_backup_dir(dir, sizeof dir, subdir))
        return false;

    base = path_local_basename(src_path);
    if (!base || !base[0])
        base = "bddtool-save";
    hash = path_stable_hash(src_path && src_path[0] ? src_path : base);
    snprintf(name, sizeof name, "%s.%08X%s", base, hash, suffix ? suffix : "");
    path_join(out, outsz, dir, name);
    return out[0] != '\0';
}
