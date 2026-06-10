#include "bg_editor.h"
#include "bg_editor_globals.h"

#include "Core/bdd_core.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"
#include "Core/path_utils.h"

#include <climits>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BDD_EDITOR_VIEW_EDGE_PADDING 500

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

void bdd_get_world_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max)
{
    *wx_min = INT_MAX;
    *wx_max = INT_MIN;
    *wy_min = INT_MAX;
    *wy_max = INT_MIN;
    if (!g_have_bdb || g_no == 0) return;
    for (int i = 0; i < g_no; i++) {
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im) continue;
        if (o->depth < *wx_min) *wx_min = o->depth;
        if (o->depth + im->w > *wx_max) *wx_max = o->depth + im->w;
        if (o->sy < *wy_min) *wy_min = o->sy;
        if (o->sy + im->h > *wy_max) *wy_max = o->sy + im->h;
    }
}

static int bdd_parse_module_line(int index, char *name, int name_sz,
                                 int *x1, int *x2, int *y1, int *y2)
{
    BddCoreModule module;

    if (index < 0 || index >= g_bdb_num_modules)
        return 0;
    if (!bdd_core_parse_module_line(g_bdb_modules[index], &module))
        return 0;
    if (name && name_sz > 0) {
        snprintf(name, (size_t)name_sz, "%s", module.name);
    }
    if (x1) *x1 = module.x1;
    if (x2) *x2 = module.x2;
    if (y1) *y1 = module.y1;
    if (y2) *y2 = module.y2;
    return 1;
}

static int bdd_object_module_info(int obj_index, char *name, int name_sz,
                                  int *x1, int *x2, int *y1, int *y2)
{
    Obj *o;
    Img *im;

    if (obj_index < 0 || obj_index >= g_no || g_bdb_num_modules <= 0)
        return 0;

    o = &g_obj[obj_index];
    im = img_find(o->ii);
    if (!im)
        return 0;

    BddCoreModule module;
    if (bdd_core_find_fitting_module_in_lines((const char (*)[256])g_bdb_modules,
                                              g_bdb_num_modules,
                                              o->depth, o->sy,
                                              im->w, im->h,
                                              &module) >= 0) {
        if (name && name_sz > 0) snprintf(name, (size_t)name_sz, "%s", module.name);
        if (x1) *x1 = module.x1;
        if (x2) *x2 = module.x2;
        if (y1) *y1 = module.y1;
        if (y2) *y2 = module.y2;
        return 1;
    }

    return 0;
}

static int bdd_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return 0;
    size_t nlen = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen && h[i]) {
            char a = h[i];
            char b = needle[i];
            if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
            if (a != b) break;
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

static int bdd_current_stage_contains(const char *needle)
{
    return bdd_contains_ci(g_name, needle) ||
           bdd_contains_ci(g_bdb_path, needle) ||
           bdd_contains_ci(g_bdd_path, needle) ||
           bdd_contains_ci(g_stage_internal_name, needle);
}

static int bdd_current_stage_is_battle(void)
{
    return bdd_current_stage_contains("BATTLE");
}

static int bdd_current_stage_is_forest(void)
{
    return bdd_current_stage_contains("FOREST");
}

static int bdd_current_stage_is_tower_runtime(void)
{
    if (bdd_current_stage_contains("TOWER2"))
        return 1;
    return bdd_current_stage_contains("TWGCLOUD");
}

static int bdd_runtime_info_set(int *ox, int *oy, float *scroll,
                                int x, int y, float s)
{
    if (ox) *ox = x;
    if (oy) *oy = y;
    if (scroll) *scroll = s;
    return 1;
}

static int bdd_module_name_contains(const char *needle)
{
    for (int i = 0; i < g_bdb_num_modules; i++) {
        BddCoreModule module = {};
        if (!bdd_core_parse_module_line(g_bdb_modules[i], &module))
            continue;
        if (bdd_contains_ci(module.name, needle))
            return 1;
    }
    return 0;
}

static const char *bdd_bgnd_stage_label(void)
{
    if (bdd_module_name_contains("WOOD")) return "forest_mod";
    if (bdd_module_name_contains("PLANE")) return "tower_mod";
    if (bdd_module_name_contains("BAT")) return "battle_mod";
    if (bdd_module_name_contains("DPUL")) return "dedpool_mod";
    return NULL;
}

static int bdd_parse_signed16_hex_token(const char *tok, int *out)
{
    char *end = NULL;
    long v;
    if (!tok || !tok[0] || !out) return 0;
    v = strtol(tok, &end, 16);
    if (end == tok) return 0;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    v &= 0xffff;
    if (v & 0x8000) v -= 0x10000;
    *out = (int)v;
    return 1;
}

static int bdd_bgnd_lst_word_value(const char *line, int *out)
{
    char prefix[256];
    char *word;
    char *ctx = NULL;
    char *tok;
    char *last = NULL;
    size_t n;

    if (!line || !out) return 0;
    word = strstr((char *)line, ".word");
    if (!word) return 0;
    n = (size_t)(word - line);
    if (n >= sizeof prefix) n = sizeof prefix - 1;
    memcpy(prefix, line, n);
    prefix[n] = '\0';

    tok = strtok_s(prefix, " \t\r\n", &ctx);
    while (tok) {
        last = tok;
        tok = strtok_s(NULL, " \t\r\n", &ctx);
    }
    return bdd_parse_signed16_hex_token(last, out);
}

struct BddAsmExprParser {
    const char *p;
    int scrrgt;
    int wy_offset;
};

static void bdd_expr_skip_ws(BddAsmExprParser *ep)
{
    while (ep && ep->p && isspace((unsigned char)*ep->p)) ep->p++;
}

static int bdd_expr_parse_expr(BddAsmExprParser *ep, int *out);

static int bdd_expr_parse_number_or_symbol(BddAsmExprParser *ep, int *out)
{
    char tok[64];
    int n = 0;

    if (!ep || !ep->p || !out) return 0;
    bdd_expr_skip_ws(ep);
    if (*ep->p == '>') {
        ep->p++;
        while (isxdigit((unsigned char)*ep->p) && n < (int)sizeof(tok) - 1)
            tok[n++] = *ep->p++;
        tok[n] = '\0';
        return bdd_parse_signed16_hex_token(tok, out);
    }

    while ((isalnum((unsigned char)*ep->p) || *ep->p == '_') &&
           n < (int)sizeof(tok) - 1) {
        tok[n++] = *ep->p++;
    }
    tok[n] = '\0';
    if (n <= 0) return 0;

    if (strcasecmp(tok, "scrrgt") == 0) {
        *out = ep->scrrgt;
        return 1;
    }
    if (strcasecmp(tok, "wy_offset") == 0) {
        *out = ep->wy_offset;
        return 1;
    }

    if (n > 1 && (tok[n - 1] == 'h' || tok[n - 1] == 'H')) {
        tok[n - 1] = '\0';
        return bdd_parse_signed16_hex_token(tok, out);
    }

    char *end = NULL;
    long v = strtol(tok, &end, 10);
    if (end == tok || *end) return 0;
    *out = (int)v;
    return 1;
}

static int bdd_expr_parse_factor(BddAsmExprParser *ep, int *out)
{
    int sign = 1;
    int v = 0;

    if (!ep || !out) return 0;
    bdd_expr_skip_ws(ep);
    while (*ep->p == '+' || *ep->p == '-') {
        if (*ep->p == '-') sign = -sign;
        ep->p++;
        bdd_expr_skip_ws(ep);
    }
    if (*ep->p == '(') {
        ep->p++;
        if (!bdd_expr_parse_expr(ep, &v)) return 0;
        bdd_expr_skip_ws(ep);
        if (*ep->p == ')') ep->p++;
    } else if (!bdd_expr_parse_number_or_symbol(ep, &v)) {
        return 0;
    }
    *out = v * sign;
    return 1;
}

static int bdd_expr_parse_term(BddAsmExprParser *ep, int *out)
{
    int v = 0;
    if (!bdd_expr_parse_factor(ep, &v)) return 0;
    for (;;) {
        int rhs = 0;
        char op;
        bdd_expr_skip_ws(ep);
        op = *ep->p;
        if (op != '*' && op != '/') break;
        ep->p++;
        if (!bdd_expr_parse_factor(ep, &rhs)) return 0;
        if (op == '*') v *= rhs;
        else {
            if (rhs == 0) return 0;
            v /= rhs;
        }
    }
    *out = v;
    return 1;
}

static int bdd_expr_parse_expr(BddAsmExprParser *ep, int *out)
{
    int v = 0;
    if (!bdd_expr_parse_term(ep, &v)) return 0;
    for (;;) {
        int rhs = 0;
        char op;
        bdd_expr_skip_ws(ep);
        op = *ep->p;
        if (op != '+' && op != '-') break;
        ep->p++;
        if (!bdd_expr_parse_term(ep, &rhs)) return 0;
        if (op == '+') v += rhs;
        else v -= rhs;
    }
    *out = v;
    return 1;
}

static int bdd_eval_asm_expr(const char *expr, int scrrgt, int wy_offset, int *out)
{
    BddAsmExprParser ep;
    if (!expr || !out) return 0;
    ep.p = expr;
    ep.scrrgt = scrrgt;
    ep.wy_offset = wy_offset;
    if (!bdd_expr_parse_expr(&ep, out)) return 0;
    bdd_expr_skip_ws(&ep);
    if (*ep.p && *ep.p != ',' && *ep.p != ';' && *ep.p != '\r' && *ep.p != '\n')
        return 0;
    if (*out > 32767) *out = ((*out) & 0xffff) - 0x10000;
    return 1;
}

static int bdd_bgnd_asm_word_expr_value(const char *line, int scrrgt,
                                        int wy_offset, int *out)
{
    char expr[256];
    char *word;
    char *end;
    size_t n;

    if (!line || !out) return 0;
    word = strstr((char *)line, ".word");
    if (!word) return 0;
    word += 5;
    while (*word && isspace((unsigned char)*word)) word++;
    end = word;
    while (*end && *end != ';' && *end != '\r' && *end != '\n') end++;
    n = (size_t)(end - word);
    if (n >= sizeof expr) n = sizeof expr - 1;
    memcpy(expr, word, n);
    expr[n] = '\0';
    return bdd_eval_asm_expr(expr, scrrgt, wy_offset, out);
}

static int bdd_line_is_label(const char *line, const char *label)
{
    char tok[96];
    int n = 0;
    if (!line || !label) return 0;
    while (*line && isspace((unsigned char)*line)) line++;
    if (!*line || *line == ';' || *line == '*') return 0;
    while (*line && !isspace((unsigned char)*line) && *line != ':' &&
           n < (int)sizeof(tok) - 1) {
        tok[n++] = *line++;
    }
    tok[n] = '\0';
    return n > 0 && strcasecmp(tok, label) == 0;
}

static int bdd_line_has_label_token(const char *line, const char *label)
{
    char token[96];
    int n = 0;
    if (!line || !label || !label[0]) return 0;
    for (const char *p = line; ; p++) {
        int c = *p;
        if (isalnum((unsigned char)c) || c == '_') {
            if (n < (int)sizeof token - 1)
                token[n++] = (char)c;
            continue;
        }
        if (n > 0) {
            token[n] = '\0';
            if (strcasecmp(token, label) == 0)
                return 1;
            n = 0;
        }
        if (!c || c == ';')
            break;
    }
    return 0;
}

static int bdd_line_has_active_word(const char *line)
{
    const char *word;
    const char *semi;
    if (!line) return 0;
    word = strstr(line, ".word");
    if (!word) return 0;
    semi = strchr(line, ';');
    return !semi || semi > word;
}

static int bdd_read_bgnd_camera_from_file(const char *path, const char *label,
                                          int is_lst, int scrrgt,
                                          int wy_offset, int *camera_x,
                                          int *camera_y)
{
    FILE *f;
    char line[512];
    int in_block = 0;
    int word_count = 0;
    int y = 0, x = 0;

    if (!path || !label || !camera_x || !camera_y) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        int value = 0;
        if (!in_block) {
            int label_hit = is_lst ? bdd_line_has_label_token(line, label)
                                   : bdd_line_is_label(line, label);
            if (label_hit && (!is_lst || (!strstr(line, ".long") && !strstr(line, ".word"))))
                in_block = 1;
            continue;
        }
        if (!bdd_line_has_active_word(line))
            continue;
        word_count++;
        if (word_count != 3 && word_count != 4)
            continue;
        if (is_lst) {
            if (!bdd_bgnd_lst_word_value(line, &value))
                continue;
        } else {
            if (!bdd_bgnd_asm_word_expr_value(line, scrrgt, wy_offset, &value))
                continue;
        }
        if (word_count == 3) y = value;
        else {
            x = value;
            fclose(f);
            *camera_x = x;
            *camera_y = y;
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int bdd_read_bgnd_scroll_limits_from_file(const char *path, const char *label,
                                                 int is_lst, int scrrgt,
                                                 int wy_offset,
                                                 int *scroll_left,
                                                 int *scroll_right)
{
    FILE *f;
    char line[512];
    int in_block = 0;
    int word_count = 0;
    int left = 0, right = 0;

    if (!path || !label || !scroll_left || !scroll_right) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        int value = 0;
        if (!in_block) {
            int label_hit = is_lst ? bdd_line_has_label_token(line, label)
                                   : bdd_line_is_label(line, label);
            if (label_hit && (!is_lst || (!strstr(line, ".long") && !strstr(line, ".word"))))
                in_block = 1;
            continue;
        }
        if (!bdd_line_has_active_word(line))
            continue;
        word_count++;
        if (word_count != 5 && word_count != 6)
            continue;
        if (is_lst) {
            if (!bdd_bgnd_lst_word_value(line, &value))
                continue;
        } else {
            if (!bdd_bgnd_asm_word_expr_value(line, scrrgt, wy_offset, &value))
                continue;
        }
        if (word_count == 5) {
            left = value;
        } else {
            right = value;
            fclose(f);
            if (right < left) return 0;
            *scroll_left = left;
            *scroll_right = right;
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int bdd_bgnd_root_has_source(const char *root)
{
    char path[512];
    if (!root || !root[0]) return 0;
    path_join(path, sizeof path, root, "src-refactor\\src\\BGND.ASM");
    if (stage_file_exists(path)) return 1;
    path_join(path, sizeof path, root, "src\\BGND.ASM");
    if (stage_file_exists(path)) return 1;
    path_join(path, sizeof path, root, "src-refactor\\src\\BGND.LST");
    if (stage_file_exists(path)) return 1;
    path_join(path, sizeof path, root, "src\\BGND.LST");
    return stage_file_exists(path) ? 1 : 0;
}

static int bdd_choose_bgnd_source_root(const char *candidate, char *out, size_t outsz)
{
    if (!candidate || !candidate[0] || !out || outsz == 0)
        return 0;
    if (!bdd_bgnd_root_has_source(candidate))
        return 0;
    snprintf(out, outsz, "%s", candidate);
    return 1;
}

static void bdd_stage_root_from_loaded_path(char *out, size_t outsz)
{
    char data_dir[512];
    char loaded_root[512];
    const char *base = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!base || !base[0]) return;
    stage_dirname(base, data_dir, sizeof data_dir);
    stage_dirname(data_dir, loaded_root, sizeof loaded_root);

    if (bdd_choose_bgnd_source_root(loaded_root, out, outsz))
        return;

    bdd_choose_bgnd_source_root(g_stage_mk2_root, out, outsz);
}

static int bdd_try_bgnd_camera_candidate(const char *root, const char *rel,
                                         const char *label, int is_lst,
                                         int *camera_x, int *camera_y)
{
    char path[512];
    if (!root || !root[0] || !rel || !label) return 0;
    path_join(path, sizeof path, root, rel);
    if (!stage_file_exists(path)) return 0;
    return bdd_read_bgnd_camera_from_file(path, label, is_lst, 399, 0xe0,
                                          camera_x, camera_y);
}

static int bdd_try_bgnd_scroll_limits_candidate(const char *root, const char *rel,
                                                const char *label, int is_lst,
                                                int *scroll_left,
                                                int *scroll_right)
{
    char path[512];
    if (!root || !root[0] || !rel || !label) return 0;
    path_join(path, sizeof path, root, rel);
    if (!stage_file_exists(path)) return 0;
    return bdd_read_bgnd_scroll_limits_from_file(path, label, is_lst, 399, 0xe0,
                                                 scroll_left, scroll_right);
}

static int bdd_read_bgnd_stage_start_camera(const char *label,
                                            int *camera_x, int *camera_y)
{
    char root[512];
    bdd_stage_root_from_loaded_path(root, sizeof root);
    if (!root[0]) return 0;

    if (bdd_try_bgnd_camera_candidate(root, "src-refactor\\src\\BGND.LST",
                                      label, 1, camera_x, camera_y))
        return 1;
    if (bdd_try_bgnd_camera_candidate(root, "src\\BGND.LST",
                                      label, 1, camera_x, camera_y))
        return 1;
    if (bdd_try_bgnd_camera_candidate(root, "src\\BGND.ASM",
                                      label, 0, camera_x, camera_y))
        return 1;
    if (bdd_try_bgnd_camera_candidate(root, "src-refactor\\src\\BGND.ASM",
                                      label, 0, camera_x, camera_y))
        return 1;
    return 0;
}

static int bdd_read_bgnd_stage_scroll_limits(const char *label,
                                             int *scroll_left,
                                             int *scroll_right)
{
    char root[512];
    bdd_stage_root_from_loaded_path(root, sizeof root);
    if (!root[0]) return 0;

    if (bdd_try_bgnd_scroll_limits_candidate(root, "src-refactor\\src\\BGND.LST",
                                             label, 1, scroll_left, scroll_right))
        return 1;
    if (bdd_try_bgnd_scroll_limits_candidate(root, "src\\BGND.LST",
                                             label, 1, scroll_left, scroll_right))
        return 1;
    if (bdd_try_bgnd_scroll_limits_candidate(root, "src\\BGND.ASM",
                                             label, 0, scroll_left, scroll_right))
        return 1;
    if (bdd_try_bgnd_scroll_limits_candidate(root, "src-refactor\\src\\BGND.ASM",
                                             label, 0, scroll_left, scroll_right))
        return 1;
    return 0;
}

static int bdd_stage_module_runtime_info(const char *name, int *ox, int *oy, float *scroll)
{
    if (!name)
        return 0;

    if (bdd_current_stage_is_battle()) {
        if (strcasecmp(name, "BAT1") == 0 || strcasecmp(name, "BAT1BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 0, 0x93, 0.8125f);
        if (strcasecmp(name, "BAT2") == 0 || strcasecmp(name, "BAT2BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 249, 0x04, 0.5f);
        if (strcasecmp(name, "BAT4") == 0 || strcasecmp(name, "BAT4BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 222, 0x61, 0.25f);
        if (strcasecmp(name, "BAT5") == 0 || strcasecmp(name, "BAT5BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 667, 0x40, 0.09375f);
        if (strcasecmp(name, "BAT6") == 0 || strcasecmp(name, "BAT6BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 401, 0x5a, 0.0625f);
        if (strcasecmp(name, "BAT7") == 0 || strcasecmp(name, "BAT7BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 424, 0x1a, 0.0f);
    }

    if (bdd_current_stage_is_tower_runtime()) {
        if (strcasecmp(name, "PLANE4") == 0 || strcasecmp(name, "PLANE4BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 0x16d, -0x1f, 0.6875f);
        if (strcasecmp(name, "PLANE5") == 0 || strcasecmp(name, "PLANE5BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 0, -0x21, 0.625f);
    }

    if (bdd_current_stage_is_forest()) {
        if (strcasecmp(name, "wood1") == 0 || strcasecmp(name, "wood1BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 0, -0x36, 1.0f);
        if (strcasecmp(name, "wood2") == 0 || strcasecmp(name, "wood2BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 135, -0x1a, 0.75f);
        if (strcasecmp(name, "wood4") == 0 || strcasecmp(name, "wood4BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 111, 0x14, 0.625f);
        if (strcasecmp(name, "wood5") == 0 || strcasecmp(name, "wood5BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 215, 0x21, 0.5f);
        if (strcasecmp(name, "wood6") == 0 || strcasecmp(name, "wood6BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 281, 0x3a, 0.3125f);
        if (strcasecmp(name, "wood7") == 0 || strcasecmp(name, "wood7BMOD") == 0)
            return bdd_runtime_info_set(ox, oy, scroll, 378, 0x52, 0.0f);
    }

    return 0;
}

int bdd_object_runtime_origin(int obj_index, int *rx, int *ry)
{
    int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
    char module_name[64];

    if (obj_index < 0 || obj_index >= g_no)
        return 0;

    if (bdd_object_module_info(obj_index, module_name, (int)sizeof module_name, &mx1, &mx2, &my1, &my2)) {
        int ox = 0, oy = 0;
        int local_x = g_obj[obj_index].depth - mx1;
        int local_y = g_obj[obj_index].sy - my1;
        if (bdd_stage_module_runtime_info(module_name, &ox, &oy, NULL)) {
            if (rx) *rx = ox + local_x;
            if (ry) *ry = oy + local_y;
        } else {
            if (rx) *rx = local_x;
            if (ry) *ry = local_y;
        }
        return 1;
    }

    if (rx) *rx = g_obj[obj_index].depth;
    if (ry) *ry = g_obj[obj_index].sy;
    return 0;
}

static int bdd_runtime_module_min_y(void)
{
    int min_y = INT_MAX;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!bdd_parse_module_line(m, NULL, 0, &x1, &x2, &y1, &y2))
            continue;
        if (y1 < min_y) min_y = y1;
    }
    return min_y;
}

static int bdd_runtime_module_is_detached(int my1)
{
    int min_y = bdd_runtime_module_min_y();
    if (min_y == INT_MAX)
        return 0;
    return (my1 - min_y) > 1536;
}

static int bdd_runtime_main_edit_bottom(void)
{
    int min_y = bdd_runtime_module_min_y();
    int bottom = 0;
    if (min_y == INT_MAX)
        return 254;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        int h;
        if (!bdd_parse_module_line(m, NULL, 0, &x1, &x2, &y1, &y2))
            continue;
        if (bdd_runtime_module_is_detached(y1))
            continue;
        h = y2 - y1 + 1;
        if (h > bottom) bottom = h;
    }
    return bottom > 0 ? bottom : 254;
}

static int bdd_runtime_detached_shelf_top(int module_y1)
{
    int top = bdd_runtime_main_edit_bottom() + 96;

    for (int m = 0; m < g_bdb_num_modules; m++) {
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (!bdd_parse_module_line(m, NULL, 0, &x1, &x2, &y1, &y2))
            continue;
        if (!bdd_runtime_module_is_detached(y1))
            continue;
        if (y1 >= module_y1)
            continue;
        top += (y2 - y1 + 1) + 48;
    }
    return top;
}

int bdd_object_editor_origin(int obj_index, int *ex, int *ey)
{
    int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
    char module_name[64];

    if (obj_index < 0 || obj_index >= g_no)
        return 0;

    if (g_runtime_layout_view &&
        bdd_object_module_info(obj_index, module_name, (int)sizeof module_name, &mx1, &mx2, &my1, &my2)) {
        int ox = 0, oy = 0;
        int local_x = g_obj[obj_index].depth - mx1;
        int local_y = g_obj[obj_index].sy - my1;
        if (bdd_stage_module_runtime_info(module_name, &ox, &oy, NULL)) {
            if (ex) *ex = ox + local_x;
            if (ey) *ey = oy + local_y;
            return 1;
        }
        if (ex) *ex = local_x;
        if (ey) {
            if (bdd_runtime_module_is_detached(my1))
                *ey = bdd_runtime_detached_shelf_top(my1) + local_y;
            else
                *ey = local_y;
        }
        return 1;
    }

    if (ex) *ex = g_obj[obj_index].depth;
    if (ey) *ey = g_obj[obj_index].sy;
    return 1;
}

void bdd_get_runtime_layout_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max)
{
    *wx_min = INT_MAX;
    *wx_max = INT_MIN;
    *wy_min = INT_MAX;
    *wy_max = INT_MIN;
    if (!g_have_bdb || g_no == 0) return;

    for (int i = 0; i < g_no; i++) {
        int rx = 0, ry = 0;
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        bdd_object_runtime_origin(i, &rx, &ry);
        if (rx < *wx_min) *wx_min = rx;
        if (rx + im->w > *wx_max) *wx_max = rx + im->w;
        if (ry < *wy_min) *wy_min = ry;
        if (ry + im->h > *wy_max) *wy_max = ry + im->h;
    }
}

void bdd_get_editor_layout_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max)
{
    *wx_min = INT_MAX;
    *wx_max = INT_MIN;
    *wy_min = INT_MAX;
    *wy_max = INT_MIN;
    if (!g_have_bdb || g_no == 0) return;

    for (int i = 0; i < g_no; i++) {
        int ex = 0, ey = 0;
        Img *im = img_find(g_obj[i].ii);
        if (!im) continue;
        bdd_object_editor_origin(i, &ex, &ey);
        if (ex < *wx_min) *wx_min = ex;
        if (ex + im->w > *wx_max) *wx_max = ex + im->w;
        if (ey < *wy_min) *wy_min = ey;
        if (ey + im->h > *wy_max) *wy_max = ey + im->h;
    }
}

void bdd_get_editor_view_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max)
{
    bdd_get_editor_layout_bounds(wx_min, wx_max, wy_min, wy_max);

    if (*wx_min == INT_MAX || *wx_max == INT_MIN || *wy_min == INT_MAX || *wy_max == INT_MIN) {
        *wx_min = 0; *wx_max = 1280;
        *wy_min = 0; *wy_max = 720;
    }

    *wx_min -= BDD_EDITOR_VIEW_EDGE_PADDING;
    *wx_max += BDD_EDITOR_VIEW_EDGE_PADDING;
    *wy_min -= BDD_EDITOR_VIEW_EDGE_PADDING;
    *wy_max += BDD_EDITOR_VIEW_EDGE_PADDING;
}

void bdd_clamp_editor_view(int win_w, int win_h, int zoom, int *view_x, int *view_y)
{
    int wx_min = 0, wx_max = 1280, wy_min = 0, wy_max = 720;
    int span_w, span_h;
    int min_x, max_x, min_y, max_y;
    int visible_w, visible_h;

    if (!g_have_bdb || g_no <= 0 || !view_x || !view_y || zoom <= 0)
        return;

    bdd_get_editor_view_bounds(&wx_min, &wx_max, &wy_min, &wy_max);

    visible_w = win_w / zoom;
    visible_h = win_h / zoom;
    if (visible_w < 1) visible_w = 1;
    if (visible_h < 1) visible_h = 1;

    span_w = wx_max - wx_min;
    span_h = wy_max - wy_min;

    if (span_w <= visible_w) {
        *view_x = wx_min - (visible_w - span_w) / 2;
    } else {
        min_x = wx_min;
        max_x = wx_max - visible_w;
        if (*view_x < min_x) *view_x = min_x;
        if (*view_x > max_x) *view_x = max_x;
    }

    if (span_h <= visible_h) {
        *view_y = wy_min - (visible_h - span_h) / 2;
    } else {
        min_y = wy_min;
        max_y = wy_max - visible_h;
        if (*view_y < min_y) *view_y = min_y;
        if (*view_y > max_y) *view_y = max_y;
    }
}

void bdd_get_game_preview_bounds(int *wx_min, int *wx_max, int *wy_min, int *wy_max)
{
    int x0 = 0, x1 = 400, y0 = 0, y1 = 254;

    if (g_runtime_layout_view)
        bdd_get_runtime_layout_bounds(&x0, &x1, &y0, &y1);
    else
        bdd_get_world_bounds(&x0, &x1, &y0, &y1);
    if (x0 == INT_MAX || x1 == INT_MIN || y0 == INT_MAX || y1 == INT_MIN) {
        x0 = 0;
        x1 = 400;
        y0 = 0;
        y1 = 254;
    }
    {
        const char *label = bdd_bgnd_stage_label();
        int scroll_left = 0;
        int scroll_right = 0;
        if (label && bdd_read_bgnd_stage_scroll_limits(label, &scroll_left, &scroll_right)) {
            x0 = scroll_left;
            x1 = scroll_right + 400;
        }
    }
    {
        int start_x = 0;
        int start_y = 0;
        if (bdd_get_stage_start_camera(&start_x, &start_y)) {
            if (start_x < x0) x0 = start_x;
            if (start_x + 400 > x1) x1 = start_x + 400;
            if (start_y < y0) y0 = start_y;
            if (start_y + 254 > y1) y1 = start_y + 254;
        }
    }
    if (x1 < x0 + 400) x1 = x0 + 400;
    if (y1 < y0 + 254) y1 = y0 + 254;

    *wx_min = x0;
    *wx_max = x1;
    *wy_min = y0;
    *wy_max = y1;
}

void bdd_center_game_preview_camera(void)
{
    int wx_min = 0, wx_max = 400, wy_min = 0, wy_max = 254;
    int scroll_max, scroll_y_max;

    bdd_get_game_preview_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    scroll_max = wx_max - 400;
    scroll_y_max = wy_max - 254;

    g_scroll_pos = wx_min + (scroll_max - wx_min) / 2;
    g_game_view_y = wy_min + (scroll_y_max - wy_min) / 2;

    if (g_scroll_pos < wx_min) g_scroll_pos = wx_min;
    if (g_scroll_pos > scroll_max) g_scroll_pos = scroll_max;
    if (g_game_view_y < wy_min) g_game_view_y = wy_min;
    if (g_game_view_y > scroll_y_max) g_game_view_y = scroll_y_max;
}

int bdd_get_stage_start_camera(int *camera_x, int *camera_y)
{
    const char *label;
    int x = 0;
    int y = 0;

    if (g_stage_start_camera_enabled) {
        if (camera_x) *camera_x = g_stage_start_camera_x;
        if (camera_y) *camera_y = g_stage_start_camera_y;
        return 1;
    }

    label = bdd_bgnd_stage_label();
    if (!label)
        return 0;
    if (!bdd_read_bgnd_stage_start_camera(label, &x, &y))
        return 0;

    if (camera_x) *camera_x = x;
    if (camera_y) *camera_y = y;
    return 1;
}

void bdd_reset_game_preview_camera(void)
{
    int wx_min = 0, wx_max = 400, wy_min = 0, wy_max = 254;
    int scroll_max;
    int scroll_y_max;

    if (!bdd_get_stage_start_camera(&g_scroll_pos, &g_game_view_y)) {
        bdd_center_game_preview_camera();
        return;
    }

    bdd_get_game_preview_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
    scroll_max = wx_max - 400;
    scroll_y_max = wy_max - 254;

    if (g_scroll_pos < wx_min) g_scroll_pos = wx_min;
    if (g_scroll_pos > scroll_max) g_scroll_pos = scroll_max;
    if (g_game_view_y < wy_min) g_game_view_y = wy_min;
    if (g_game_view_y > scroll_y_max) g_game_view_y = scroll_y_max;
}

/* Find which module rectangle an object falls in (first-fit), -1 if none. */
static int bdd_object_module_index(int obj_index)
{
    if (obj_index < 0 || obj_index >= g_no || g_bdb_num_modules <= 0)
        return -1;
    Img *im = img_find(g_obj[obj_index].ii);
    if (!im)
        return -1;
    BddCoreModule module;
    return bdd_core_find_fitting_module_in_lines((const char (*)[256])g_bdb_modules,
                                                 g_bdb_num_modules,
                                                 g_obj[obj_index].depth, g_obj[obj_index].sy,
                                                 im->w, im->h, &module);
}

/* A module acts as a parallax plane: its scroll rate is the most common layer
   scroll factor among the objects inside it, so every object in the module
   scrolls together. This makes authored modules drive parallax on any stage,
   not just the hard-coded BATTLE modules. */
static float bdd_module_plane_scroll_factor(int module_index)
{
    if (module_index < 0)
        return 1.0f;
    int counts[256] = {0};
    int best_layer = -1, best_count = 0;
    for (int i = 0; i < g_no; i++) {
        if (bdd_object_module_index(i) != module_index)
            continue;
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        if (++counts[layer] > best_count) {
            best_count = counts[layer];
            best_layer = layer;
        }
    }
    if (best_layer < 0)
        return 1.0f;
    return bdd_core_mk2_scroll_factor(best_layer);
}

float bdd_object_game_scroll_factor(int obj_index)
{
    if (obj_index < 0 || obj_index >= g_no)
        return 1.0f;

    int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
    char module_name[64];
    float scroll = 0.0f;
    /* Known BGND runtime modules keep their authored scroll rates. */
    if (bdd_object_module_info(obj_index, module_name, (int)sizeof module_name, &mx1, &mx2, &my1, &my2) &&
        bdd_stage_module_runtime_info(module_name, NULL, NULL, &scroll))
        return scroll;

    /* Otherwise the object's module is its parallax plane. */
    int module_index = bdd_object_module_index(obj_index);
    if (module_index >= 0)
        return bdd_module_plane_scroll_factor(module_index);

    /* No module: fall back to the object's own layer. */
    return bdd_core_mk2_scroll_factor((g_obj[obj_index].wx >> 8) & 0xFF);
}

void bdd_object_game_origin(int obj_index, int *gx, int *gy)
{
    if (g_runtime_layout_view && bdd_object_runtime_origin(obj_index, gx, gy))
        return;
    if (obj_index >= 0 && obj_index < g_no) {
        if (gx) *gx = g_obj[obj_index].depth;
        if (gy) *gy = g_obj[obj_index].sy;
    }
}

static int bdd_object_image_label_equals(int obj_index, const char *label)
{
    if (obj_index < 0 || obj_index >= g_no || !label)
        return 0;
    Img *im = img_find(g_obj[obj_index].ii);
    return im && im->label[0] && strcasecmp(im->label, label) == 0;
}

static int bdd_object_image_label_is_any(int obj_index, const char *const *labels, int count)
{
    for (int i = 0; i < count; i++)
        if (bdd_object_image_label_equals(obj_index, labels[i]))
            return 1;
    return 0;
}

static int bdd_object_is_stage_floor(int obj_index)
{
    if (bdd_current_stage_is_battle())
        return bdd_object_image_label_equals(obj_index, "FL_BATTL");
    if (bdd_current_stage_is_forest())
        return bdd_object_image_label_equals(obj_index, "FL_FORST");
    if (bdd_current_stage_is_tower_runtime())
        return bdd_object_image_label_equals(obj_index, "FL_TOW");
    return 0;
}

int bdd_object_uses_runtime_floor_y(int obj_index)
{
    return bdd_object_is_stage_floor(obj_index);
}

int bdd_runtime_floor_screen_y(int floor_y)
{
    int start_x = 0;
    int start_y = 0;
    if (bdd_get_stage_start_camera(&start_x, &start_y))
        return floor_y + start_y - g_game_view_y;
    return floor_y - g_game_view_y;
}

int bdd_object_game_screen_y(int obj_index, int game_y)
{
    if (bdd_object_uses_runtime_floor_y(obj_index))
        return bdd_runtime_floor_screen_y(game_y);
    return game_y - g_game_view_y;
}

/* Draw rank follows the BGND display-list plane order for runtime preview:
   far background first, floor at its -1/floor_code slot, foreground later. */
int bdd_object_runtime_draw_rank(int obj_index)
{
    int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
    char module_name[64];

    if (obj_index < 0 || obj_index >= g_no)
        return 1000000;

    if (bdd_current_stage_is_battle()) {
        static const char *const battle_props[] = {
            "RUBLE1", "BURN_VDA", "SKULLS", "SKELTS",
            "ROCK_VDA", "BURN6_VDA", "RUBLE2"
        };
        if (bdd_object_module_info(obj_index, module_name, (int)sizeof module_name,
                                   &mx1, &mx2, &my1, &my2)) {
            if (strcasecmp(module_name, "BAT7") == 0 || strcasecmp(module_name, "BAT7BMOD") == 0)
                return 10;
            if (strcasecmp(module_name, "BAT6") == 0 || strcasecmp(module_name, "BAT6BMOD") == 0)
                return 20;
            if (strcasecmp(module_name, "BAT5") == 0 || strcasecmp(module_name, "BAT5BMOD") == 0)
                return 30;
            if (strcasecmp(module_name, "BAT4") == 0 || strcasecmp(module_name, "BAT4BMOD") == 0)
                return 40;
            if (strcasecmp(module_name, "BAT2") == 0 || strcasecmp(module_name, "BAT2BMOD") == 0)
                return 50;
            if (strcasecmp(module_name, "BAT1") == 0 || strcasecmp(module_name, "BAT1BMOD") == 0)
                return 70;
        }
        if (bdd_object_image_label_is_any(obj_index, battle_props,
                                          (int)(sizeof battle_props / sizeof battle_props[0])))
            return 50;
        if (bdd_object_is_stage_floor(obj_index))
            return 60;
    }

    if (bdd_current_stage_is_forest()) {
        if (bdd_object_module_info(obj_index, module_name, (int)sizeof module_name,
                                   &mx1, &mx2, &my1, &my2)) {
            if (strcasecmp(module_name, "wood7") == 0 || strcasecmp(module_name, "wood7BMOD") == 0)
                return 10;
            if (strcasecmp(module_name, "wood6") == 0 || strcasecmp(module_name, "wood6BMOD") == 0)
                return 20;
            if (strcasecmp(module_name, "wood5") == 0 || strcasecmp(module_name, "wood5BMOD") == 0)
                return 30;
            if (strcasecmp(module_name, "wood4") == 0 || strcasecmp(module_name, "wood4BMOD") == 0)
                return 40;
            if (strcasecmp(module_name, "wood2") == 0 || strcasecmp(module_name, "wood2BMOD") == 0)
                return 50;
            if (strcasecmp(module_name, "wood1") == 0 || strcasecmp(module_name, "wood1BMOD") == 0)
                return 70;
        }
        if (bdd_object_is_stage_floor(obj_index))
            return 60;
    }

    if (bdd_current_stage_is_tower_runtime()) {
        static const char *const tower_clouds[] = { "CLOUD1A", "CLOUD1B", "CLOUD1C", "CLOUD1D" };
        static const char *const tower_monk[] = {
            "MONKTORSO", "MONK1", "MONK2", "MONK3", "MONK4", "MONK5", "MONK6", "MONK7"
        };
        if (bdd_object_module_info(obj_index, module_name, (int)sizeof module_name,
                                   &mx1, &mx2, &my1, &my2)) {
            if (strcasecmp(module_name, "PLANE5") == 0 || strcasecmp(module_name, "PLANE5BMOD") == 0)
                return 30;
            if (strcasecmp(module_name, "PLANE4") == 0 || strcasecmp(module_name, "PLANE4BMOD") == 0)
                return 50;
        }
        if (bdd_object_image_label_is_any(obj_index, tower_clouds,
                                          (int)(sizeof tower_clouds / sizeof tower_clouds[0])))
            return 10;
        if (bdd_object_is_stage_floor(obj_index))
            return 40;
        if (bdd_object_image_label_is_any(obj_index, tower_monk,
                                          (int)(sizeof tower_monk / sizeof tower_monk[0])))
            return 45;
        if (bdd_object_image_label_equals(obj_index, "STATUE2") ||
            bdd_object_image_label_equals(obj_index, "STATUE1") ||
            bdd_object_image_label_equals(obj_index, "FLAMEA1"))
            return 55;
    }

    return 500 + obj_index;
}
