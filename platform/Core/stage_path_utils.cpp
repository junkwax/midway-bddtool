#include "Core/path_utils.h"
#include "Core/stage_paths.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static bool path_has_ext_ci(const char *path, const char *ext)
{
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    return plen >= elen && strcasecmp(path + plen - elen, ext) == 0;
}

static void path_replace_ext(char *out, size_t outsz, const char *path, const char *ext)
{
    if (!out || outsz == 0) return;
    snprintf(out, outsz, "%s", path ? path : "");
    char *slash = strrchr(out, '/');
    char *backslash = strrchr(out, '\\');
    char *sep = slash;
    if (!sep || (backslash && backslash > sep)) sep = backslash;
    char *dot = strrchr(out, '.');
    if (!dot || (sep && dot < sep)) {
        size_t n = strlen(out);
        snprintf(out + n, outsz - n, "%s", ext ? ext : "");
        return;
    }
    snprintf(dot, outsz - (size_t)(dot - out), "%s", ext ? ext : "");
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

void derive_stage_pair_paths(const char *path,
                             char *bdd, size_t bddsz,
                             char *bdb, size_t bdbsz)
{
    if (bdd && bddsz) bdd[0] = '\0';
    if (bdb && bdbsz) bdb[0] = '\0';
    if (!path || !path[0]) return;

    if (path_has_ext_ci(path, ".bdb")) {
        snprintf(bdb, bdbsz, "%s", path);
        path_replace_ext(bdd, bddsz, path, ".BDD");
    } else if (path_has_ext_ci(path, ".bdd")) {
        snprintf(bdd, bddsz, "%s", path);
        path_replace_ext(bdb, bdbsz, path, ".BDB");
    } else {
        snprintf(bdd, bddsz, "%s", path);
        path_replace_ext(bdb, bdbsz, path, ".BDB");
    }
}

const char *path_basename_ptr(const char *path)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    return base;
}
