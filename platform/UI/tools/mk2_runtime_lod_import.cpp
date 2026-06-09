#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/stage_paths.h"
#include "undo_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

static bool path_is_absolute_for_import(const char *path)
{
    if (!path || !path[0]) return false;
#ifdef _WIN32
    if ((path[0] == '\\' || path[0] == '/') ||
        (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
         path[1] == ':'))
        return true;
    return false;
#else
    return path[0] == '/';
#endif
}

static void path_dirname_for_import(const char *path, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s", path ? path : "");
    char *sep = strrchr(out, '\\');
    char *sep2 = strrchr(out, '/');
    if (!sep || (sep2 && sep2 > sep)) sep = sep2;
    if (sep) *sep = '\0';
    else snprintf(out, outsz, ".");
}

static void trim_import_token(char *token)
{
    if (!token) return;
    char *start = token;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n' ||
           *start == '"' || *start == '\'')
        start++;
    if (start != token)
        memmove(token, start, strlen(start) + 1);

    size_t len = strlen(token);
    while (len > 0) {
        char c = token[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' ||
            c == '\'' || c == ',' || c == ';' || c == ')' || c == ']') {
            token[--len] = '\0';
            continue;
        }
        break;
    }
}

static bool resolve_lod_img_path(const char *lod_dir, const char *token,
                                 char *out, size_t outsz)
{
    char clean[512];
    snprintf(clean, sizeof clean, "%s", token ? token : "");
    trim_import_token(clean);
    out[0] = '\0';
    if (!clean[0]) return false;

    if (path_is_absolute_for_import(clean) && stage_file_exists(clean)) {
        snprintf(out, outsz, "%s", clean);
        return true;
    }
    if (!path_is_absolute_for_import(clean) && stage_file_exists(clean)) {
        snprintf(out, outsz, "%s", clean);
        return true;
    }

    const char *base = path_basename_ptr(clean);
    const char *rel = path_is_absolute_for_import(clean) ? base : clean;
    if (lod_dir && lod_dir[0]) {
        path_join(out, outsz, lod_dir, rel);
        if (stage_file_exists(out)) return true;
        if (base != rel) {
            path_join(out, outsz, lod_dir, base);
            if (stage_file_exists(out)) return true;
        }

        char parent[512];
        path_dirname_for_import(lod_dir, parent, sizeof parent);
        if (parent[0] && strcasecmp(parent, lod_dir) != 0) {
            path_join(out, outsz, parent, rel);
            if (stage_file_exists(out)) return true;
            if (base != rel) {
                path_join(out, outsz, parent, base);
                if (stage_file_exists(out)) return true;
            }
        }
    }

    const char *envs[] = { "IMGDIR", "MK2_IMGDIR", "MIDWAY_IMGDIR" };
    for (int i = 0; i < 3; i++) {
        const char *dir = getenv(envs[i]);
        if (!dir || !dir[0]) continue;
        path_join(out, outsz, dir, rel);
        if (stage_file_exists(out)) return true;
        if (base != rel) {
            path_join(out, outsz, dir, base);
            if (stage_file_exists(out)) return true;
        }
    }

    out[0] = '\0';
    return false;
}

struct ImportedImgRange {
    std::string path;
    int start;
    int end;
};

static int find_imported_range(const std::vector<ImportedImgRange> &ranges, const char *path)
{
    for (size_t i = 0; i < ranges.size(); i++)
        if (strcasecmp(ranges[i].path.c_str(), path) == 0)
            return (int)i;
    return -1;
}

static void uppercase_ascii_inplace(char *s)
{
    if (!s) return;
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
}

static void lod_mark_label_in_range(int start, int end, const char *raw_label, int *matched)
{
    char label[64];
    snprintf(label, sizeof label, "%s", raw_label ? raw_label : "");
    trim_import_token(label);
    uppercase_ascii_inplace(label);
    if (!label[0]) return;
    for (int i = start; i < end && i < g_ni; i++) {
        if (strcasecmp(g_img[i].label, label) != 0)
            continue;
        g_img[i].lod_ref = 1;
        if (matched) (*matched)++;
        return;
    }
}

static void lod_basename_no_ext_upper(const char *path, char *out, size_t outsz)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(out, outsz, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    uppercase_ascii_inplace(out);
}

static bool lod_text_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen && h[i]) {
            unsigned char a = (unsigned char)h[i];
            unsigned char b = (unsigned char)needle[i];
            if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 32);
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

void lod_tag_imported_range(int start, int end, const char *lod_path)
{
    if (start < 0 || end <= start || !lod_path || !lod_path[0]) return;
    char lod_base[64];
    lod_basename_no_ext_upper(lod_path, lod_base, sizeof lod_base);
    if (!lod_base[0]) return;

    for (int i = start; i < end && i < g_ni; i++) {
        g_img[i].lod_ref = 1;
        if (g_img[i].source[0] && lod_text_contains_ci(g_img[i].source, lod_base))
            continue;

        char old_source[64];
        snprintf(old_source, sizeof old_source, "%s", g_img[i].source);
        if (old_source[0])
            snprintf(g_img[i].source, sizeof g_img[i].source, "%.28s/%.28s",
                     old_source, lod_base);
        else
            snprintf(g_img[i].source, sizeof g_img[i].source, "%.56s", lod_base);
    }
}

static int lod_mark_reference_line(int start, int end, const char *refs, int *matched)
{
    if (start < 0 || end <= start || !refs) return 0;
    int labels = 0;
    const char *p = refs;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char label[64];
        size_t o = 0;
        while (*p && *p != ',' && *p != '\r' && *p != '\n' && o + 1 < sizeof label)
            label[o++] = *p++;
        label[o] = '\0';
        trim_import_token(label);
        if (label[0]) {
            labels++;
            lod_mark_label_in_range(start, end, label, matched);
        }
        while (*p && *p != ',') p++;
    }
    return labels;
}

bool img_label_exists_ci(const char *label)
{
    if (!label || !label[0]) return false;
    char clean[64];
    snprintf(clean, sizeof clean, "%s", label);
    trim_import_token(clean);
    uppercase_ascii_inplace(clean);
    if (!clean[0]) return false;
    for (int i = 0; i < g_ni; i++) {
        if (g_img[i].label[0] && strcasecmp(g_img[i].label, clean) == 0)
            return true;
    }
    return false;
}

static bool runtime_source_extract_lod_token(const char *source, char *out, size_t outsz)
{
    if (!source || !out || outsz == 0) return false;
    out[0] = '\0';
    const char *dot = NULL;
    for (const char *p = source; *p; p++) {
        if ((p[0] == '.' || p[0] == '/') &&
            (p[1] == 'L' || p[1] == 'l') &&
            (p[2] == 'O' || p[2] == 'o') &&
            (p[3] == 'D' || p[3] == 'd')) {
            dot = p;
            break;
        }
    }
    if (!dot) return false;
    const char *start = dot;
    while (start > source) {
        char c = start[-1];
        if (c == ' ' || c == '\t' || c == ',' || c == ';' || c == '"' || c == '\'' ||
            c == '(' || c == ')' || c == '[' || c == ']')
            break;
        start--;
    }
    const char *end = dot + 4;
    size_t n = (size_t)(end - start);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    trim_import_token(out);
    return out[0] != '\0';
}

static bool runtime_resolve_lod_path(const char *token, char *out, size_t outsz)
{
    if (!token || !out || outsz == 0) return false;
    out[0] = '\0';
    if (stage_file_exists(token)) {
        snprintf(out, outsz, "%s", token);
        return true;
    }
    return mk2_find_sibling_data_file(token, out, outsz);
}

static bool runtime_label_wanted(const std::vector<std::string> &wanted, const char *raw_label, char *clean_out, size_t clean_outsz)
{
    char label[64];
    snprintf(label, sizeof label, "%s", raw_label ? raw_label : "");
    trim_import_token(label);
    uppercase_ascii_inplace(label);
    if (!label[0]) return false;
    if (clean_out && clean_outsz > 0)
        snprintf(clean_out, clean_outsz, "%s", label);
    for (size_t i = 0; i < wanted.size(); i++) {
        if (strcasecmp(wanted[i].c_str(), label) == 0)
            return true;
    }
    return false;
}

static bool runtime_lod_ref_line_needs_import(const std::vector<std::string> &wanted, const char *refs)
{
    if (!refs) return false;
    const char *p = refs;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char label[64];
        size_t o = 0;
        while (*p && *p != ',' && *p != '\r' && *p != '\n' && o + 1 < sizeof label)
            label[o++] = *p++;
        label[o] = '\0';
        char clean[64];
        if (runtime_label_wanted(wanted, label, clean, sizeof clean) && !img_label_exists_ci(clean))
            return true;
        while (*p && *p != ',') p++;
    }
    return false;
}

static bool resolve_lod_asm_img_path(const char *lod_dir, const char *raw_token,
                                     char *out, size_t outsz)
{
    char token[512];
    snprintf(token, sizeof token, "%s", raw_token ? raw_token : "");
    trim_import_token(token);
    if (!token[0]) return false;
    char *dot = strrchr(token, '.');
    if (dot)
        snprintf(dot, sizeof token - (size_t)(dot - token), ".IMG");
    else
        strncat(token, ".IMG", sizeof token - strlen(token) - 1);
    return resolve_lod_img_path(lod_dir, token, out, outsz);
}

static int runtime_lod_import_current_source(const char *source_img,
                                             const char *source_tag,
                                             const char *refs,
                                             const std::vector<std::string> &wanted,
                                             std::vector<std::string> &imported_paths,
                                             int *matched_labels)
{
    if (!source_img || !source_img[0] ||
        !runtime_lod_ref_line_needs_import(wanted, refs))
        return 0;

    bool already = false;
    for (size_t i = 0; i < imported_paths.size(); i++) {
        if (strcasecmp(imported_paths[i].c_str(), source_img) == 0) {
            already = true;
            break;
        }
    }
    if (already) return 0;

    int start = g_ni;
    int pal_start = g_n_pals;
    int n = import_img_file(source_img, false);
    if (n <= 0) return 0;

    imported_paths.push_back(source_img);
    lod_tag_imported_range(start, g_ni, source_tag && source_tag[0] ? source_tag : source_img);
    runtime_actor_mark_preview_import_range(start, pal_start,
                                            start, g_ni,
                                            source_tag && source_tag[0] ? source_tag : source_img);
    lod_mark_reference_line(start, g_ni, refs, matched_labels);
    return n;
}

static int import_runtime_lod_sources_from_lod(const char *lod_path, const std::vector<std::string> &wanted)
{
    FILE *f = fopen(lod_path, "r");
    if (!f) return 0;

    char lod_dir[512];
    path_dirname_for_import(lod_path, lod_dir, sizeof lod_dir);
    char last_img[1024] = "";
    std::vector<std::string> imported_paths;
    int imported = 0;
    int matched_lines = 0;
    int matched_labels = 0;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        comment = strchr(line, '#');
        if (comment) *comment = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "--->", 4) == 0) {
            int n = runtime_lod_import_current_source(last_img, lod_path, p + 4,
                                                      wanted, imported_paths,
                                                      &matched_labels);
            if (n > 0) {
                imported += n;
                matched_lines++;
            }
            continue;
        }
        if (strncmp(p, "FRM>", 4) == 0) {
            int n = runtime_lod_import_current_source(last_img, lod_path, p + 4,
                                                      wanted, imported_paths,
                                                      &matched_labels);
            if (n > 0) {
                imported += n;
                matched_lines++;
            }
            continue;
        }
        if (strncmp(p, "ASM>", 4) == 0) {
            char asm_token[512];
            snprintf(asm_token, sizeof asm_token, "%s", p + 4);
            trim_import_token(asm_token);
            if (resolve_lod_asm_img_path(lod_dir, asm_token, last_img, sizeof last_img))
                continue;
            last_img[0] = '\0';
            continue;
        }

        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')
                p++;
            if (!*p) break;
            char token[512] = "";
            char *dst = token;
            size_t left = sizeof token - 1;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && left > 0) { *dst++ = *p++; left--; }
                if (*p == '"') p++;
            } else {
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && left > 0) {
                    *dst++ = *p++;
                    left--;
                }
            }
            *dst = '\0';
            trim_import_token(token);
            if (!path_has_ext_ci(token, ".img"))
                continue;
            char resolved[1024];
            if (resolve_lod_img_path(lod_dir, token, resolved, sizeof resolved))
                snprintf(last_img, sizeof last_img, "%s", resolved);
            else
                last_img[0] = '\0';
        }
    }
    fclose(f);
    fprintf(stderr, "runtime-lod: %s -> %d sprites (%d matching ref lines, %d labels)\n",
            lod_path, imported, matched_lines, matched_labels);
    return imported;
}

int import_runtime_lod_sources_for_active_guides(bool save_undo)
{
    if (!mk2_current_stage_has_known_runtime_extras())
        return 0;
    tower_runtime_guides_init_once();
    int count = tower_runtime_guide_count();
    if (count <= 0) return 0;

    std::vector<std::string> wanted;
    std::vector<std::string> lod_paths;
    for (int i = 0; i < count; i++) {
        char label[64];
        snprintf(label, sizeof label, "%s", g_tower_runtime_guides[i].asset);
        trim_import_token(label);
        uppercase_ascii_inplace(label);
        if (label[0]) {
            bool seen = false;
            for (size_t j = 0; j < wanted.size(); j++)
                if (strcasecmp(wanted[j].c_str(), label) == 0) { seen = true; break; }
            if (!seen) wanted.push_back(label);
        }

        char lod_token[512];
        if (runtime_source_extract_lod_token(g_tower_runtime_guides[i].source, lod_token, sizeof lod_token)) {
            char lod_path[1024];
            if (runtime_resolve_lod_path(lod_token, lod_path, sizeof lod_path)) {
                bool seen = false;
                for (size_t j = 0; j < lod_paths.size(); j++)
                    if (strcasecmp(lod_paths[j].c_str(), lod_path) == 0) { seen = true; break; }
                if (!seen) lod_paths.push_back(lod_path);
            }
        }
    }
    if (wanted.empty() || lod_paths.empty())
        return 0;

    if (save_undo) undo_save_ex("Import Runtime LOD Sources");
    int imported = 0;
    for (size_t i = 0; i < lod_paths.size(); i++)
        imported += import_runtime_lod_sources_from_lod(lod_paths[i].c_str(), wanted);
    if (imported > 0) {
        g_show_images = true;
        g_dirty = 1;
        g_need_rebuild = 1;
    }
    return imported;
}

int import_runtime_lod_source_labels(const char *lod_token,
                                     const char *const *labels,
                                     int label_count,
                                     bool save_undo)
{
    if (!lod_token || !lod_token[0] || !labels || label_count <= 0)
        return 0;

    std::vector<std::string> wanted;
    wanted.reserve((size_t)label_count);
    for (int i = 0; i < label_count; i++) {
        char label[64];
        snprintf(label, sizeof label, "%s", labels[i] ? labels[i] : "");
        trim_import_token(label);
        uppercase_ascii_inplace(label);
        if (!label[0] || img_label_exists_ci(label))
            continue;
        bool seen = false;
        for (size_t j = 0; j < wanted.size(); j++)
            if (strcasecmp(wanted[j].c_str(), label) == 0) { seen = true; break; }
        if (!seen)
            wanted.push_back(label);
    }
    if (wanted.empty())
        return 0;

    char lod_path[1024];
    if (!runtime_resolve_lod_path(lod_token, lod_path, sizeof lod_path))
        return 0;

    if (save_undo) undo_save_ex("Import Runtime LOD Sources");
    int imported = import_runtime_lod_sources_from_lod(lod_path, wanted);
    if (imported > 0) {
        g_show_images = true;
        g_dirty = 1;
        g_need_rebuild = 1;
    }
    return imported;
}

static bool runtime_guides_have_any_source_image(void)
{
    tower_runtime_guides_init_once();
    int count = tower_runtime_guide_count();
    for (int i = 0; i < count; i++) {
        if (img_label_exists_ci(g_tower_runtime_guides[i].asset))
            return true;
    }
    return false;
}

static int mark_runtime_guide_images_as_lod_refs(void)
{
    tower_runtime_guides_init_once();
    int marked = 0;
    int count = tower_runtime_guide_count();
    for (int i = 0; i < g_ni; i++) {
        if (!g_img[i].label[0]) continue;
        for (int gi = 0; gi < count; gi++) {
            if (strcasecmp(g_img[i].label, g_tower_runtime_guides[gi].asset) == 0) {
                if (!g_img[i].lod_ref) {
                    g_img[i].lod_ref = 1;
                    marked++;
                }
                break;
            }
        }
    }
    return marked;
}

extern "C" int bg_editor_autoload_lod_assets(void)
{
    if (!g_have_bdb)
        return 0;
    mk2_runtime_autoload_stage_recipe();
    if (!mk2_current_stage_has_known_runtime_extras())
        return 0;

    int old_dirty = g_dirty;
    int old_need_rebuild = g_need_rebuild;
    bool old_palette_dirty = g_mk2_palette_sync_dirty;
    int imported = import_runtime_lod_sources_for_active_guides(false);

    if (imported <= 0 && mk2_current_stage_is_battle() && !runtime_guides_have_any_source_image()) {
        char battle_img_path[512];
        if (mk2_find_sibling_data_file("BATTLE.IMG", battle_img_path, sizeof battle_img_path)) {
            int start = g_ni;
            int pal_start = g_n_pals;
            int n = import_img_file(battle_img_path, false);
            if (n > 0) {
                lod_tag_imported_range(start, g_ni, "MK7MIL.LOD");
                runtime_actor_mark_preview_import_range(start, pal_start,
                                                        start, g_ni,
                                                        "BATTLE.IMG");
                imported += n;
            }
        }
    }

    int marked = mark_runtime_guide_images_as_lod_refs();
    int baked = 0;
    if (imported > 0)
        baked = mk2_bake_runtime_guides_to_bdb(false, false);

    if (imported > 0 || baked > 0 || marked > 0) {
        sync_bdb_header_counts();
        g_dirty = 1;
        g_need_rebuild = 1;
        g_show_images = true;
        snprintf(g_toast_msg, sizeof g_toast_msg,
                 "Loaded runtime LOD art: %d sprites, %d object(s)",
                 imported, baked);
        g_toast_timer = 3.0f;
        fprintf(stderr, "autoload-lod: imported=%d baked=%d marked=%d\n",
                imported, baked, marked);
    }
    g_dirty = old_dirty;
    g_mk2_palette_sync_dirty = old_palette_dirty;
    if (imported > 0 || baked > 0 || marked > 0)
        g_need_rebuild = 1;
    else
        g_need_rebuild = old_need_rebuild;
    return imported + baked + marked;
}

