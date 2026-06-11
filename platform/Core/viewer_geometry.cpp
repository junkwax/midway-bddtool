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
#define strncasecmp _strnicmp
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

static int bdd_bdb_has_module_named(const char *name)
{
    if (!name || !name[0]) return 0;
    for (int i = 0; i < g_bdb_num_modules; i++) {
        BddCoreModule module = {};
        if (!bdd_core_parse_module_line(g_bdb_modules[i], &module))
            continue;
        if (strcasecmp(module.name, name) == 0)
            return 1;
    }
    return 0;
}

/* Defined later, alongside the other BGND.ASM parse helpers. */
static int bdd_discover_stage_mod_label(char *out, size_t outsz);

/* Identify which BGND.ASM <stage>_mod block describes the loaded level by
   matching each block's baklst <name>BMOD modules against the BDB's modules.
   Fully data-driven so every MK2 stage is recognized, not a fixed shortlist.
   Cached per loaded path + module count. */
static const char *bdd_bgnd_stage_label(void)
{
    static char cached_key[640] = "";
    static char cached_label[64] = "";
    static int cached_valid = 0;
    char key[640];
    const char *base = g_bdb_path[0] ? g_bdb_path : (g_bdd_path[0] ? g_bdd_path : "");

    snprintf(key, sizeof key, "%d|%s", g_bdb_num_modules, base);
    if (cached_valid && strcmp(cached_key, key) == 0)
        return cached_label[0] ? cached_label : NULL;

    snprintf(cached_key, sizeof cached_key, "%s", key);
    cached_label[0] = '\0';
    cached_valid = 1;
    if (bdd_discover_stage_mod_label(cached_label, sizeof cached_label))
        return cached_label;
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
    const char *word;
    const char *start;
    const char *end;
    char tok[64];
    size_t n;
    size_t tok_len;

    if (!line || !out) return 0;
    word = strstr(line, ".word");
    if (!word) return 0;
    n = (size_t)(word - line);
    if (n >= sizeof prefix) n = sizeof prefix - 1;
    memcpy(prefix, line, n);
    prefix[n] = '\0';

    end = prefix + n;
    while (end > prefix && isspace((unsigned char)end[-1])) end--;
    if (end <= prefix) return 0;
    start = end;
    while (start > prefix && !isspace((unsigned char)start[-1])) start--;
    tok_len = (size_t)(end - start);
    if (tok_len == 0 || tok_len >= sizeof tok) return 0;
    memcpy(tok, start, tok_len);
    tok[tok_len] = '\0';
    return bdd_parse_signed16_hex_token(tok, out);
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

/* Runtime parallax for a stage's background planes is authored in MK2's
   BGND.ASM, not hardcoded here:
     - The <stage>_mod block lists each baklst plane as ".long <name>BMOD"
       followed by ".word x,y" giving the plane's screen-placement offset.
     - The <stage>_scroll table holds one 16.16 fixed-point scroll rate per
       baklst index (read top-to-bottom as index 8..0). The playfield scrolls
       at BDD_BGND_PLAYFIELD_SCROLL, so a plane's parallax factor is
       scroll[index] / playfield.
   We parse that block once per stage and cache it, so the offsets/rates stay
   in sync with the source instead of a transcribed constant table. */
#define BDD_STAGE_PLANE_MAX 16
#define BDD_BGND_PLAYFIELD_SCROLL 0x20000
#define BDD_BGND_MOD_HEADER_LONGS 4   /* calla, scroll table, dlists, bak1mods */
#define BDD_BGND_SCROLL_ENTRIES 9     /* baklst index 8..0 */

typedef struct {
    char name[32];   /* module name, trailing "BMOD" stripped */
    int baklst;      /* baklst plane index (1..8) */
    int ox;
    int oy;
    float scroll;
    int draw_rank;   /* runtime draw order from the dlists, -1 if not placed */
} BddStagePlane;

typedef struct {
    char label[64];          /* <stage>_mod label this table was built for */
    char source_path[512];   /* resolved BGND.ASM path */
    int valid;
    BddStagePlane planes[BDD_STAGE_PLANE_MAX];
    int plane_count;
    int floor_rank;          /* draw rank of the -1/floor_code dlists slot, -1 if none */
    char floor_label[32];    /* floor SAG label from <stage>_floor_info, "" if none */
    char floor_palette[32];  /* floor palette label from <stage>_floor_info, "" if none */
    int floor_info_valid;    /* floor_y/floor_height were parsed from floor_info */
    int floor_y;             /* skew Y position word from <stage>_floor_info */
    int floor_height;        /* floor height word from <stage>_floor_info */
    int floor_skew_shift;    /* sra N in the floor's skew_calla (shear = -dx>>N), 0 = none */
    int camera_valid;        /* <stage>_mod words 3,4 captured (init worldy/worldx) */
    int camera_x, camera_y;
    int limits_valid;        /* <stage>_mod words 5,6 captured (scroll left/right) */
    int scroll_left, scroll_right;
    char calla_label[64];    /* <stage>_mod calla routine (header long #1) */
    float baklst_scroll[BDD_BGND_SCROLL_ENTRIES]; /* parallax factor per baklst 0..8 */
} BddStageModuleTable;

/* scrrgt/wy_offset substituted into <stage>_mod header expressions, matching the
   MK2 build (scrrgt = visible width-1, wy_offset = world->screen Y bias). */
#define BDD_BGND_SCRRGT 399
#define BDD_BGND_WY_OFFSET 0xe0

static BddStageModuleTable g_stage_module_cache;

static void bdd_strip_bmod_suffix(const char *in, char *out, size_t outsz)
{
    size_t len;
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!in) return;
    snprintf(out, outsz, "%s", in);
    len = strlen(out);
    if (len >= 4 && strcasecmp(out + len - 4, "BMOD") == 0)
        out[len - 4] = '\0';
}

/* Find an ".op" directive that is not commented out, returning the operand
   text (first non-space after the directive) or NULL. */
static const char *bdd_bgnd_asm_active_directive(const char *line, const char *op)
{
    const char *dir;
    const char *semi;
    if (!line || !op) return NULL;
    dir = strstr(line, op);
    if (!dir) return NULL;
    semi = strchr(line, ';');
    if (semi && semi < dir) return NULL;
    dir += strlen(op);
    while (*dir && isspace((unsigned char)*dir)) dir++;
    return dir;
}

/* Copy the first operand token of a ".long" line (label or numeric literal). */
static int bdd_bgnd_asm_long_token(const char *line, char *out, size_t outsz)
{
    const char *operand = bdd_bgnd_asm_active_directive(line, ".long");
    size_t n = 0;
    if (!operand || !out || outsz == 0) return 0;
    while (operand[n] && operand[n] != ',' && operand[n] != ';' &&
           !isspace((unsigned char)operand[n])) {
        if (n + 1 >= outsz) break;
        out[n] = operand[n];
        n++;
    }
    out[n] = '\0';
    return n > 0;
}

/* Parse a numeric ".long" operand (">" hex or decimal) into a 32-bit value. */
static int bdd_bgnd_asm_long_number(const char *line, long *out)
{
    const char *operand = bdd_bgnd_asm_active_directive(line, ".long");
    char *end = NULL;
    long v;
    if (!operand || !out) return 0;
    if (*operand == '>') {
        v = strtol(operand + 1, &end, 16);
    } else if (isdigit((unsigned char)*operand)) {
        v = strtol(operand, &end, 10);
    } else {
        return 0;
    }
    if (end == operand + (*operand == '>' ? 1 : 0)) return 0;
    *out = v;
    return 1;
}

/* Parse the two comma-separated values of a baklst ".word x,y" offset line. */
static int bdd_bgnd_asm_word_pair_value(const char *line, int scrrgt,
                                        int wy_offset, int *a, int *b)
{
    const char *operand = bdd_bgnd_asm_active_directive(line, ".word");
    BddAsmExprParser ep;
    int va = 0, vb = 0;
    if (!operand || !a || !b) return 0;
    ep.p = operand;
    ep.scrrgt = scrrgt;
    ep.wy_offset = wy_offset;
    if (!bdd_expr_parse_expr(&ep, &va)) return 0;
    bdd_expr_skip_ws(&ep);
    if (*ep.p != ',') return 0;
    ep.p++;
    if (!bdd_expr_parse_expr(&ep, &vb)) return 0;
    if (va > 32767) va = (va & 0xffff) - 0x10000;
    if (vb > 32767) vb = (vb & 0xffff) - 0x10000;
    *a = va;
    *b = vb;
    return 1;
}

/* Walk the <stage>_mod block: capture each baklst plane's name/offset/index and
   the name of the stage's scroll table. */
static int bdd_parse_stage_mod_block(const char *path, const char *label,
                                     BddStageModuleTable *table,
                                     char *scroll_label, size_t scroll_label_sz,
                                     char *dlists_label, size_t dlists_label_sz,
                                     char *calla_label, size_t calla_label_sz)
{
    FILE *f;
    char line[512];
    int in_block = 0;
    int long_seen = 0;
    int baklst_num = 0;
    int pending = -1;
    int header_word = 0;

    if (!path || !label || !table) return 0;
    f = fopen(path, "r");
    if (!f) return 0;

    table->plane_count = 0;
    while (fgets(line, sizeof line, f)) {
        if (!in_block) {
            if (bdd_line_is_label(line, label))
                in_block = 1;
            continue;
        }

        /* Header words precede every ".long": 1=autoerase, 2=ground y,
           3=initial worldy, 4=initial worldx, 5=scroll left, 6=scroll right. */
        if (long_seen == 0) {
            int hv = 0;
            if (bdd_bgnd_asm_word_expr_value(line, BDD_BGND_SCRRGT, BDD_BGND_WY_OFFSET, &hv) &&
                bdd_bgnd_asm_active_directive(line, ".word")) {
                header_word++;
                switch (header_word) {
                    case 3: table->camera_y = hv; break;
                    case 4: table->camera_x = hv; table->camera_valid = 1; break;
                    case 5: table->scroll_left = hv; break;
                    case 6:
                        table->scroll_right = hv;
                        if (hv >= table->scroll_left) table->limits_valid = 1;
                        break;
                    default: break;
                }
                continue;
            }
        }

        const char *long_op = bdd_bgnd_asm_active_directive(line, ".long");
        if (long_op) {
            char token[64];
            if (!bdd_bgnd_asm_long_token(line, token, sizeof token))
                continue;
            long_seen++;
            if (long_seen <= BDD_BGND_MOD_HEADER_LONGS) {
                /* Header longs: calla(1), scroll table(2), dlists(3), bak1mods(4). */
                if (long_seen == 1 && calla_label)
                    snprintf(calla_label, calla_label_sz, "%s", token);
                if (long_seen == 2 && scroll_label)
                    snprintf(scroll_label, scroll_label_sz, "%s", token);
                if (long_seen == 3 && dlists_label)
                    snprintf(dlists_label, dlists_label_sz, "%s", token);
                continue;
            }
            /* baklst entries: symbol names only; a numeric (e.g. >ffffffff) ends. */
            if (token[0] == '>' || isdigit((unsigned char)token[0]))
                break;
            baklst_num++;
            if (strcasecmp(token, "skip_bakmod") == 0)
                continue;
            {
                size_t tl = strlen(token);
                if (tl < 4 || strcasecmp(token + tl - 4, "BMOD") != 0)
                    break;   /* unexpected non-module symbol ends the list */
            }
            if (table->plane_count < BDD_STAGE_PLANE_MAX) {
                BddStagePlane *p = &table->planes[table->plane_count];
                bdd_strip_bmod_suffix(token, p->name, sizeof p->name);
                p->baklst = baklst_num;
                p->ox = 0;
                p->oy = 0;
                p->scroll = 0.0f;
                p->draw_rank = -1;
                pending = table->plane_count;
                table->plane_count++;
            }
            continue;
        }

        if (pending >= 0 && bdd_bgnd_asm_active_directive(line, ".word")) {
            int ox = 0, oy = 0;
            if (bdd_bgnd_asm_word_pair_value(line, BDD_BGND_SCRRGT, BDD_BGND_WY_OFFSET, &ox, &oy)) {
                table->planes[pending].ox = ox;
                table->planes[pending].oy = oy;
            }
            pending = -1;
        }
    }
    fclose(f);
    return table->plane_count > 0;
}

/* Read the <stage>_scroll table's 9 ".long" rates (index 8..0, top to bottom). */
static int bdd_parse_stage_scroll_table(const char *path, const char *scroll_label,
                                        long out[BDD_BGND_SCROLL_ENTRIES])
{
    FILE *f;
    char line[512];
    int in_block = 0;
    int count = 0;

    if (!path || !scroll_label || !scroll_label[0]) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in_block) {
            if (bdd_line_is_label(line, scroll_label))
                in_block = 1;
            continue;
        }
        long v = 0;
        if (bdd_bgnd_asm_long_number(line, &v)) {
            if (count < BDD_BGND_SCROLL_ENTRIES)
                out[count] = v;
            count++;
            if (count >= BDD_BGND_SCROLL_ENTRIES)
                break;
        } else if (bdd_bgnd_asm_active_directive(line, ".long")) {
            break;   /* non-numeric .long ends the table */
        }
    }
    fclose(f);
    return count >= BDD_BGND_SCROLL_ENTRIES;
}

/* Walk the dlists_<stage> display list and assign a runtime draw rank to each
   present background plane and to the floor slot, in display order (far plane
   first). Ranks step by 10 so gameplay-object overlays can interleave. */
static void bdd_parse_stage_dlists(const char *path, const char *dlists_label,
                                   BddStageModuleTable *table)
{
    FILE *f;
    char line[512];
    int in_block = 0;
    int rank = 10;

    table->floor_rank = -1;
    if (!path || !dlists_label || !dlists_label[0] || !table) return;
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char token[64];
        if (!in_block) {
            if (bdd_line_is_label(line, dlists_label))
                in_block = 1;
            continue;
        }
        if (!bdd_bgnd_asm_active_directive(line, ".long"))
            continue;
        if (!bdd_bgnd_asm_long_token(line, token, sizeof token))
            continue;

        if (strcasecmp(token, "-1") == 0) {
            /* floor_code slot */
            table->floor_rank = rank;
            rank += 10;
            continue;
        }
        if (strncasecmp(token, "baklst", 6) == 0) {
            int baklst = atoi(token + 6);
            int placed = 0;
            for (int i = 0; i < table->plane_count; i++) {
                if (table->planes[i].baklst == baklst) {
                    table->planes[i].draw_rank = rank;
                    placed = 1;
                    break;
                }
            }
            /* Skipped planes (no module of that index) consume no rank slot. */
            if (placed)
                rank += 10;
            continue;
        }
        /* -2 (shadows), objlst, 0, or any non-plane entry ends the background. */
        break;
    }
    fclose(f);
}

/* Copy the first operand token (up to ',' or whitespace) of a "movi" line. */
static int bdd_bgnd_asm_movi_token(const char *line, char *out, size_t outsz)
{
    const char *operand = bdd_bgnd_asm_active_directive(line, "movi");
    size_t n = 0;
    if (!operand || !out || outsz == 0) return 0;
    while (operand[n] && operand[n] != ',' && operand[n] != ';' &&
           !isspace((unsigned char)operand[n])) {
        if (n + 1 >= outsz) break;
        out[n] = operand[n];
        n++;
    }
    out[n] = '\0';
    return n > 0;
}

/* Derive the floor descriptor from <stage>_floor_info, which the mod's calla
   routine references via "movi <stage>_floor_info,a0". The block is:
       .long  FL_xxx        ; floor SAG label
       .long  xxx_p         ; floor palette label
       .word  skew_y        ; floor screen Y
       .word  height        ; floor height
   Fills table->floor_label / floor_palette / floor_y / floor_height and sets
   floor_info_valid when the two words were read. Leaves empty when the stage
   has no floor (e.g. dedpool's calla has no floor_info). */
static void bdd_parse_stage_floor_info(const char *path, const char *calla_label,
                                       BddStageModuleTable *table)
{
    FILE *f;
    char line[512];
    char floor_info_label[64] = "";
    int in_calla = 0;
    int in_info = 0;
    int long_seen = 0;
    int word_seen = 0;

    if (!path || !calla_label || !calla_label[0] || !table) return;

    /* Pass 1: find the *_floor_info label referenced inside the calla routine. */
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (!in_calla) {
            if (bdd_line_is_label(line, calla_label))
                in_calla = 1;
            continue;
        }
        char tok[64];
        if (bdd_bgnd_asm_movi_token(line, tok, sizeof tok)) {
            size_t tl = strlen(tok);
            const char *suffix = "_floor_info";
            size_t sl = strlen(suffix);
            if (tl > sl && strcasecmp(tok + tl - sl, suffix) == 0) {
                snprintf(floor_info_label, sizeof floor_info_label, "%s", tok);
                break;
            }
        }
        /* End of the calla routine without finding a floor info reference. */
        if (bdd_bgnd_asm_active_directive(line, "rets"))
            break;
    }
    fclose(f);
    if (!floor_info_label[0]) return;

    /* Pass 2: label(.long #1), palette(.long #2), skew_y(.word #1),
       height(.word #2), scroll(.long #3), skew_calla(.long #4). */
    char skew_calla_label[64] = "";
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (!in_info) {
            if (bdd_line_is_label(line, floor_info_label))
                in_info = 1;
            continue;
        }
        if (!isspace((unsigned char)line[0]) && line[0] != '.' &&
            line[0] != ';' && line[0] != '*')
            break;   /* next label ends the floor_info block */
        char tok[64];
        if (bdd_bgnd_asm_active_directive(line, ".long") &&
            bdd_bgnd_asm_long_token(line, tok, sizeof tok)) {
            long_seen++;
            if (long_seen == 1)
                snprintf(table->floor_label, sizeof table->floor_label, "%s", tok);
            else if (long_seen == 2)
                snprintf(table->floor_palette, sizeof table->floor_palette, "%s", tok);
            else if (long_seen == 4) {
                snprintf(skew_calla_label, sizeof skew_calla_label, "%s", tok);
                break;
            }
            continue;
        }
        int v = 0;
        if (bdd_bgnd_asm_active_directive(line, ".word") &&
            bdd_bgnd_asm_word_expr_value(line, BDD_BGND_SCRRGT, BDD_BGND_WY_OFFSET, &v)) {
            word_seen++;
            if (word_seen == 1)
                table->floor_y = v;
            else if (word_seen == 2) {
                table->floor_height = v;
                table->floor_info_valid = 1;
            }
        }
    }
    fclose(f);

    /* The skew_calla routine shears the floor: "sra N,a9 ; sub a9,a11" makes the
       per-line dx = -scroll>>N. Capture N so the preview can reproduce the lean. */
    if (skew_calla_label[0]) {
        FILE *sf = fopen(path, "r");
        if (sf) {
            int in_sc = 0, sc_lines = 0;
            while (fgets(line, sizeof line, sf)) {
                if (!in_sc) {
                    if (bdd_line_is_label(line, skew_calla_label)) in_sc = 1;
                    continue;
                }
                if (++sc_lines > 12) break;
                const char *s = strstr(line, "sra");
                const char *semi = strchr(line, ';');
                if (s && (!semi || semi > s) &&
                    !(isalnum((unsigned char)s[3]) || s[3] == '_')) {
                    const char *p = s + 3;
                    while (*p && isspace((unsigned char)*p)) p++;
                    int n = atoi(p);
                    if (n > 0 && n < 31) table->floor_skew_shift = n;
                    break;
                }
                if (bdd_bgnd_asm_active_directive(line, "rets") ||
                    bdd_bgnd_asm_active_directive(line, "jauc"))
                    break;
            }
            fclose(sf);
        }
    }
}

static int bdd_resolve_bgnd_asm_path(const char *root, char *out, size_t outsz)
{
    char path[512];
    if (!root || !root[0] || !out || outsz == 0) return 0;
    path_join(path, sizeof path, root, "src\\BGND.ASM");
    if (stage_file_exists(path)) { snprintf(out, outsz, "%s", path); return 1; }
    path_join(path, sizeof path, root, "src-refactor\\src\\BGND.ASM");
    if (stage_file_exists(path)) { snprintf(out, outsz, "%s", path); return 1; }
    return 0;
}

/* If the line is a column-0 label ending in "_mod", copy the label name. */
static int bdd_line_extract_mod_label(const char *line, char *out, size_t outsz)
{
    char tok[64];
    int n = 0;
    size_t len;
    if (!line || !out || outsz == 0) return 0;
    /* Top-level labels start in column 0 (no leading whitespace). */
    if (isspace((unsigned char)line[0]) || line[0] == ';' || line[0] == '*')
        return 0;
    while (line[n] && !isspace((unsigned char)line[n]) && line[n] != ':' &&
           n < (int)sizeof tok - 1) {
        tok[n] = line[n];
        n++;
    }
    tok[n] = '\0';
    len = strlen(tok);
    if (len <= 4 || strcasecmp(tok + len - 4, "_mod") != 0)
        return 0;
    snprintf(out, outsz, "%s", tok);
    return 1;
}

/* Single pass over BGND.ASM: for every <stage>_mod block, count how many of its
   baklst <name>BMOD modules exist in the loaded BDB; return the best match. */
static int bdd_discover_stage_mod_label(char *out, size_t outsz)
{
    char root[512];
    char path[512];
    FILE *f;
    char line[512];
    char cur_label[64] = "";
    char best_label[64] = "";
    int cur_long_seen = 0;
    int cur_score = 0;
    int best_score = 0;
    int in_block = 0;

    if (!out || outsz == 0) return 0;
    out[0] = '\0';
    if (g_bdb_num_modules <= 0) return 0;
    bdd_stage_root_from_loaded_path(root, sizeof root);
    if (!root[0] || !bdd_resolve_bgnd_asm_path(root, path, sizeof path))
        return 0;

    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char lbl[64];
        if (bdd_line_extract_mod_label(line, lbl, sizeof lbl)) {
            if (cur_label[0] && cur_score > best_score) {
                best_score = cur_score;
                snprintf(best_label, sizeof best_label, "%s", cur_label);
            }
            snprintf(cur_label, sizeof cur_label, "%s", lbl);
            cur_long_seen = 0;
            cur_score = 0;
            in_block = 1;
            continue;
        }
        if (!in_block)
            continue;

        if (bdd_bgnd_asm_active_directive(line, ".long")) {
            char token[64];
            if (!bdd_bgnd_asm_long_token(line, token, sizeof token))
                continue;
            cur_long_seen++;
            if (cur_long_seen <= BDD_BGND_MOD_HEADER_LONGS)
                continue;   /* calla, scroll, dlists, bak1mods */
            if (token[0] == '>' || isdigit((unsigned char)token[0])) {
                in_block = 0;   /* end marker; rest of block is non-baklst */
                continue;
            }
            {
                size_t tl = strlen(token);
                if (tl > 4 && strcasecmp(token + tl - 4, "BMOD") == 0) {
                    char want[32];
                    bdd_strip_bmod_suffix(token, want, sizeof want);
                    if (bdd_bdb_has_module_named(want))
                        cur_score++;
                }
            }
        }
    }
    if (cur_label[0] && cur_score > best_score) {
        best_score = cur_score;
        snprintf(best_label, sizeof best_label, "%s", cur_label);
    }
    fclose(f);

    if (best_score > 0) {
        snprintf(out, outsz, "%s", best_label);
        return 1;
    }
    return 0;
}

static int bdd_build_stage_module_table(BddStageModuleTable *table)
{
    const char *label = bdd_bgnd_stage_label();
    char root[512];
    char path[512];
    char scroll_label[64] = "";
    char dlists_label[64] = "";
    char calla_label[64] = "";
    long scroll[BDD_BGND_SCROLL_ENTRIES] = {0};

    if (!table) return 0;
    table->valid = 0;
    table->plane_count = 0;
    table->floor_rank = -1;
    table->floor_label[0] = '\0';
    table->floor_palette[0] = '\0';
    table->floor_info_valid = 0;
    table->floor_y = 0;
    table->floor_height = 0;
    table->floor_skew_shift = 0;
    table->camera_valid = 0;
    table->limits_valid = 0;
    table->calla_label[0] = '\0';
    if (!label) { table->label[0] = '\0'; return 0; }

    bdd_stage_root_from_loaded_path(root, sizeof root);
    if (!root[0] || !bdd_resolve_bgnd_asm_path(root, path, sizeof path))
        return 0;

    if (!bdd_parse_stage_mod_block(path, label, table, scroll_label, sizeof scroll_label,
                                   dlists_label, sizeof dlists_label,
                                   calla_label, sizeof calla_label))
        return 0;
    if (!bdd_parse_stage_scroll_table(path, scroll_label, scroll))
        return 0;
    bdd_parse_stage_dlists(path, dlists_label, table);
    bdd_parse_stage_floor_info(path, calla_label, table);
    snprintf(table->calla_label, sizeof table->calla_label, "%s", calla_label);

    for (int i = 0; i < table->plane_count; i++) {
        int pos = 8 - table->planes[i].baklst;   /* scroll table row for this plane */
        if (pos >= 0 && pos < BDD_BGND_SCROLL_ENTRIES)
            table->planes[i].scroll = (float)scroll[pos] / (float)BDD_BGND_PLAYFIELD_SCROLL;
        else
            table->planes[i].scroll = 0.0f;
    }
    /* Parallax factor per baklst index (N maps to scroll-table row 8-N). */
    for (int n = 0; n < BDD_BGND_SCROLL_ENTRIES; n++) {
        int pos = 8 - n;
        table->baklst_scroll[n] = (pos >= 0 && pos < BDD_BGND_SCROLL_ENTRIES)
                                  ? (float)scroll[pos] / (float)BDD_BGND_PLAYFIELD_SCROLL
                                  : 0.0f;
    }

    snprintf(table->label, sizeof table->label, "%s", label);
    snprintf(table->source_path, sizeof table->source_path, "%s", path);
    table->valid = 1;
    return 1;
}

static const BddStageModuleTable *bdd_get_stage_module_table(void)
{
    const char *label = bdd_bgnd_stage_label();
    if (!label) {
        g_stage_module_cache.valid = 0;
        g_stage_module_cache.label[0] = '\0';
        return NULL;
    }
    if (g_stage_module_cache.valid &&
        strcasecmp(g_stage_module_cache.label, label) == 0)
        return &g_stage_module_cache;

    if (bdd_build_stage_module_table(&g_stage_module_cache))
        return &g_stage_module_cache;
    return NULL;
}

static int bdd_stage_module_runtime_info(const char *name, int *ox, int *oy, float *scroll)
{
    const BddStageModuleTable *table;
    char want[32];

    if (!name)
        return 0;
    table = bdd_get_stage_module_table();
    if (!table)
        return 0;

    bdd_strip_bmod_suffix(name, want, sizeof want);
    for (int i = 0; i < table->plane_count; i++) {
        if (strcasecmp(table->planes[i].name, want) == 0)
            return bdd_runtime_info_set(ox, oy, scroll,
                                        table->planes[i].ox,
                                        table->planes[i].oy,
                                        table->planes[i].scroll);
    }
    return 0;
}

/* Public: enumerate the loaded stage's background planes (from <stage>_mod /
   dlists), so the block-table renderer can walk plane -> *BLKS -> blocks. */
int bdd_stage_plane_count(void)
{
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    return table ? table->plane_count : 0;
}

int bdd_stage_plane_info(int index, char *name, int name_sz,
                         int *ox, int *oy, float *scroll, int *draw_rank)
{
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    if (!table || index < 0 || index >= table->plane_count)
        return 0;
    const BddStagePlane *p = &table->planes[index];
    if (name && name_sz > 0) snprintf(name, (size_t)name_sz, "%s", p->name);
    if (ox) *ox = p->ox;
    if (oy) *oy = p->oy;
    if (scroll) *scroll = p->scroll;
    if (draw_rank) *draw_rank = p->draw_rank;
    return 1;
}

/* Public: draw rank of the floor's -1/floor_code dlists slot, INT_MAX if none. */
int bdd_stage_floor_rank(void)
{
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    return (table && table->floor_rank >= 0) ? table->floor_rank : INT_MAX;
}

/* Public: 1 when the object belongs to a known background plane module (and so
   is rendered from its *BLKS block table, not its reconstructed BDB depth). */
int bdd_object_in_background_plane(int obj_index)
{
    char module_name[64];
    int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
    if (obj_index < 0 || obj_index >= g_no)
        return 0;
    if (!bdd_object_module_info(obj_index, module_name, (int)sizeof module_name,
                                &mx1, &mx2, &my1, &my2))
        return 0;
    return bdd_stage_module_runtime_info(module_name, NULL, NULL, NULL);
}

/* dlists-derived runtime draw rank for a background module plane, or -1. */
static int bdd_stage_module_draw_rank(const char *name)
{
    const BddStageModuleTable *table;
    char want[32];

    if (!name)
        return -1;
    table = bdd_get_stage_module_table();
    if (!table)
        return -1;

    bdd_strip_bmod_suffix(name, want, sizeof want);
    for (int i = 0; i < table->plane_count; i++) {
        if (strcasecmp(table->planes[i].name, want) == 0)
            return table->planes[i].draw_rank;
    }
    return -1;
}

static int bdd_stage_floor_draw_rank(void)
{
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    return table ? table->floor_rank : -1;
}

/* Public: the loaded stage's floor descriptor from <stage>_floor_info, derived
   from vanilla BGND.ASM. Returns 1 with label/palette/y/height filled when the
   stage has a floor, 0 otherwise (e.g. dedpool). Any out pointer may be NULL. */
int bdd_stage_floor_descriptor(char *label, int label_sz,
                               char *palette, int palette_sz,
                               int *floor_y, int *floor_height)
{
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    if (!table || !table->floor_info_valid)
        return 0;
    if (label && label_sz > 0)
        snprintf(label, (size_t)label_sz, "%s", table->floor_label);
    if (palette && palette_sz > 0)
        snprintf(palette, (size_t)palette_sz, "%s", table->floor_palette);
    if (floor_y) *floor_y = table->floor_y;
    if (floor_height) *floor_height = table->floor_height;
    return 1;
}

/* Parse up to maxn comma-separated values from a ".word" line. */
static int bdd_parse_word_csv(const char *line, int *out, int maxn)
{
    const char *operand = bdd_bgnd_asm_active_directive(line, ".word");
    BddAsmExprParser ep;
    int n = 0;
    if (!operand || !out) return 0;
    ep.p = operand;
    ep.scrrgt = 0;
    ep.wy_offset = 0;
    while (n < maxn) {
        int v = 0;
        if (!bdd_expr_parse_expr(&ep, &v)) break;
        out[n++] = v;
        bdd_expr_skip_ws(&ep);
        if (*ep.p != ',') break;
        ep.p++;
    }
    return n;
}

/* Resolve src\MKBGANI.TBL (or the src-refactor copy) for the loaded stage. */
static int bdd_resolve_mkbgani_tbl(char *out, size_t outsz)
{
    char root[512];
    char path[512];
    if (!out || outsz == 0) return 0;
    bdd_stage_root_from_loaded_path(root, sizeof root);
    if (!root[0]) return 0;
    path_join(path, sizeof path, root, "src\\MKBGANI.TBL");
    if (stage_file_exists(path)) { snprintf(out, outsz, "%s", path); return 1; }
    path_join(path, sizeof path, root, "src-refactor\\src\\MKBGANI.TBL");
    if (stage_file_exists(path)) { snprintf(out, outsz, "%s", path); return 1; }
    return 0;
}

/* Public: look up a background-animation sprite's vanilla metadata in
   MKBGANI.TBL. Each entry is:
       LABEL:
           .word  w, h, xoff, yoff
           .long  <sag>
           .word  <flags/bpp>
           .long  <palette>
   Returns 1 with the requested fields filled, else 0. Any out may be NULL. */
int bdd_mkbgani_sprite_info(const char *label, int *w, int *h,
                            int *xoff, int *yoff, char *palette, int palette_sz)
{
    char path[512];
    FILE *f;
    char line[512];
    int in_entry = 0;
    int word_count = 0;
    int long_count = 0;
    int dims[4] = {0, 0, 0, 0};
    int got_dims = 0;

    if (palette && palette_sz > 0) palette[0] = '\0';
    if (!label || !label[0]) return 0;
    if (!bdd_resolve_mkbgani_tbl(path, sizeof path)) return 0;

    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in_entry) {
            if (bdd_line_is_label(line, label)) in_entry = 1;
            continue;
        }
        /* A new column-0 label ends this entry. The entry layout is:
           .word w,h,xoff,yoff / .long sag / .word flags / [.long palette].
           Some frames omit the palette .long; their palette comes from the
           usage context (BDD object), not a neighbouring table entry. */
        if (!isspace((unsigned char)line[0]) && line[0] != '.' &&
            line[0] != ';' && line[0] != '*')
            break;
        if (bdd_bgnd_asm_active_directive(line, ".word")) {
            if (++word_count == 1)
                got_dims = bdd_parse_word_csv(line, dims, 4) >= 2;
            continue;
        }
        if (bdd_bgnd_asm_active_directive(line, ".long")) {
            char tok[64];
            if (++long_count == 2) {
                if (bdd_bgnd_asm_long_token(line, tok, sizeof tok) &&
                    palette && palette_sz > 0)
                    snprintf(palette, (size_t)palette_sz, "%s", tok);
                break;
            }
        }
    }
    fclose(f);
    if (!got_dims) return 0;
    if (w) *w = dims[0];
    if (h) *h = dims[1];
    if (xoff) *xoff = dims[2];
    if (yoff) *yoff = dims[3];
    return 1;
}

/* Resolve src\BGNDTBL.ASM (or the src-refactor copy) for the loaded stage. */
static int bdd_resolve_bgndtbl_path(char *out, size_t outsz)
{
    char root[512];
    char path[512];
    if (!out || outsz == 0) return 0;
    bdd_stage_root_from_loaded_path(root, sizeof root);
    if (!root[0]) return 0;
    path_join(path, sizeof path, root, "src\\BGNDTBL.ASM");
    if (stage_file_exists(path)) { snprintf(out, outsz, "%s", path); return 1; }
    path_join(path, sizeof path, root, "src-refactor\\src\\BGNDTBL.ASM");
    if (stage_file_exists(path)) { snprintf(out, outsz, "%s", path); return 1; }
    return 0;
}

/* Public: parse a module's <module>BLKS block table from BGNDTBL.ASM. Each
   block is four .word values (flags, x, y, pal|hdr); the first block spreads
   them across separate .word lines, later blocks pack four per line, so we
   read every .word value flat and regroup into 4-tuples until 0FFFFH. */
int bdd_stage_module_blocks(const char *module, BddBgndBlock *out, int max)
{
    char path[512];
    char want[64];
    char label[72];
    FILE *f;
    char line[512];
    int in = 0, count = 0;
    int buf[4], bn = 0;
    size_t len;

    if (!module || !module[0] || !out || max <= 0) return 0;
    bdd_strip_bmod_suffix(module, want, sizeof want);
    len = strlen(want);
    if (len + 4 >= sizeof label) return 0;
    snprintf(label, sizeof label, "%sBLKS", want);

    if (!bdd_resolve_bgndtbl_path(path, sizeof path)) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, label)) in = 1;
            continue;
        }
        if (!bdd_bgnd_asm_active_directive(line, ".word")) {
            /* A new column-0 label ends the table; comments/blank lines skip. */
            if (!isspace((unsigned char)line[0]) && line[0] != ';' &&
                line[0] != '*' && line[0] != '.' && line[0] != '\0')
                break;
            continue;
        }
        int vals[8];
        int nv = bdd_parse_word_csv(line, vals, 8);
        for (int i = 0; i < nv; i++) {
            if ((vals[i] & 0xFFFF) == 0xFFFF) { in = 0; goto done; }  /* end marker */
            buf[bn++] = vals[i];
            if (bn == 4) {
                if (count < max) {
                    out[count].flags = buf[0];
                    out[count].x = buf[1];
                    out[count].y = buf[2];
                    out[count].hdr = buf[3] & 0x0FFF;
                    count++;
                }
                bn = 0;
            }
        }
    }
done:
    fclose(f);
    return count;
}

/* Extract the proc name from a "create pid_bani,<proc>" line. */
static int bdd_create_pid_bani_proc(const char *line, char *out, size_t outsz)
{
    const char *cr = strstr(line, "create");
    const char *pb = strstr(line, "pid_bani,");
    const char *semi = strchr(line, ';');
    size_t k = 0;
    const char *p;
    if (!cr || !pb || !out || outsz == 0) return 0;
    if (semi && semi < pb) return 0;
    p = pb + 9; /* strlen("pid_bani,") */
    while (*p && isspace((unsigned char)*p)) p++;
    while (p[k] && (isalnum((unsigned char)p[k]) || p[k] == '_') && k + 1 < outsz) {
        out[k] = p[k];
        k++;
    }
    out[k] = '\0';
    return k > 0;
}

/* Collect the pid_bani proc names spawned in a calla routine (capped scan;
   stops at the routine's rets/retp). */
static int bdd_collect_pid_bani_procs(const char *path, const char *calla_label,
                                      char procs[][48], int max)
{
    FILE *f;
    char line[512];
    int in = 0, n = 0, lines = 0;
    if (!path || !calla_label || !calla_label[0]) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, calla_label)) in = 1;
            continue;
        }
        if (++lines > 80) break;
        if (bdd_bgnd_asm_active_directive(line, "rets") ||
            bdd_bgnd_asm_active_directive(line, "retp"))
            break;
        char tok[48];
        if (bdd_create_pid_bani_proc(line, tok, sizeof tok) && n < max)
            snprintf(procs[n++], 48, "%s", tok);
    }
    fclose(f);
    return n;
}

/* True if the line is a column-0 label matching any name in `names` (used to
   detect where one proc's body ends and a sibling proc begins). */
static int bdd_line_is_any_of(const char *line, char names[][48], int count,
                              const char *except)
{
    if (isspace((unsigned char)line[0]) || line[0] == '.' ||
        line[0] == ';' || line[0] == '*')
        return 0;
    for (int i = 0; i < count; i++) {
        if (except && strcasecmp(names[i], except) == 0) continue;
        if (bdd_line_is_label(line, names[i])) return 1;
    }
    return 0;
}

/* Find the first "movi a_<seq>,aN" animation-table reference inside a proc,
   stopping if the scan reaches another sibling proc's label. */
static int bdd_proc_first_sequence(const char *path, const char *proc,
                                   char *seq, size_t seq_sz,
                                   char siblings[][48], int sibling_count)
{
    FILE *f;
    char line[512];
    int in = 0, lines = 0;
    if (seq && seq_sz) seq[0] = '\0';
    if (!path || !proc || !proc[0]) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, proc)) in = 1;
            continue;
        }
        /* Frame animators reference their a_* table early in the proc body;
           a tight cap avoids leaking into a later proc for velocity-driven
           procs (e.g. tower clouds) that have no frame table. */
        if (++lines > 60) break;
        if (bdd_line_is_any_of(line, siblings, sibling_count, proc))
            break;   /* reached the next sibling proc without a sequence */
        const char *m = strstr(line, "movi");
        const char *semi = strchr(line, ';');
        if (m && (!semi || semi > m)) {
            const char *op = m + 4;
            while (*op && isspace((unsigned char)*op)) op++;
            if (op[0] == 'a' && op[1] == '_') {
                size_t k = 0;
                while (op[k] && (isalnum((unsigned char)op[k]) || op[k] == '_') &&
                       k + 1 < seq_sz) {
                    seq[k] = op[k];
                    k++;
                }
                seq[k] = '\0';
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

/* Parse an a_<seq> frame-sequence table: the ".long <frame>" symbol entries. */
static int bdd_parse_anim_sequence(const char *path, const char *seq_label,
                                   char frames[][32], int maxf)
{
    FILE *f;
    char line[512];
    int in = 0, n = 0;
    if (!path || !seq_label || !seq_label[0]) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, seq_label)) in = 1;
            continue;
        }
        if (bdd_bgnd_asm_active_directive(line, ".long")) {
            char tok[32];
            if (bdd_bgnd_asm_long_token(line, tok, sizeof tok)) {
                if (tok[0] == '>' || isdigit((unsigned char)tok[0]))
                    break;   /* numeric end marker (e.g. .long 0) */
                if (strncasecmp(tok, "ani_", 4) == 0)
                    break;   /* animation control opcode, e.g. ani_jump */
                if (n < maxf) snprintf(frames[n++], 32, "%s", tok);
            }
            continue;
        }
        /* A new column-0 label ends the table. */
        if (!isspace((unsigned char)line[0]) && line[0] != '.' &&
            line[0] != ';' && line[0] != '*')
            break;
    }
    fclose(f);
    return n;
}

/* Decode a "movi >YYYYXXXX,a4" spawn coordinate. set_xy_coordinates reads a4
   as y:x (high word = Y/oypos, low word = X/oxpos, both signed 16-bit). */
static int bdd_movi_a4_coord(const char *line, int *x, int *y)
{
    const char *m = strstr(line, "movi");
    const char *semi = strchr(line, ';');
    const char *op, *reg;
    char *end = NULL;
    long v;
    int xx, yy;
    if (!m || (semi && semi < m)) return 0;
    op = m + 4;
    while (*op && isspace((unsigned char)*op)) op++;
    if (*op != '>') return 0;                 /* immediate hex only */
    v = strtol(op + 1, &end, 16);
    if (end == op + 1) return 0;
    while (*end && *end != ',') end++;
    if (*end != ',') return 0;
    reg = end + 1;
    while (*reg && isspace((unsigned char)*reg)) reg++;
    if (!(reg[0] == 'a' && reg[1] == '4' &&
          !(isalnum((unsigned char)reg[2]) || reg[2] == '_')))
        return 0;                             /* must target a4 */
    yy = (int)((v >> 16) & 0xFFFF);
    xx = (int)(v & 0xFFFF);
    if (xx > 32767) xx -= 65536;
    if (yy > 32767) yy -= 65536;
    *x = xx;
    *y = yy;
    return 1;
}

/* A column-0 "a_*" label marks the start of a proc's frame-sequence data table,
   i.e. the end of the proc's code -- a hard boundary for the proc-body scans. */
static int bdd_line_is_anim_table_label(const char *line)
{
    if (!line || isspace((unsigned char)line[0]) || line[0] == '.' ||
        line[0] == ';' || line[0] == '*')
        return 0;
    return line[0] == 'a' && line[1] == '_';
}

/* Collect static spawn coordinates (movi >y:x,a4) from a proc body. */
static int bdd_proc_positions(const char *path, const char *proc,
                              char siblings[][48], int sibling_count,
                              int *xs, int *ys, int max)
{
    FILE *f;
    char line[512];
    int in = 0, lines = 0, n = 0;
    if (!path || !proc || !proc[0]) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, proc)) in = 1;
            continue;
        }
        if (++lines > 120) break;
        if (bdd_line_is_any_of(line, siblings, sibling_count, proc))
            break;
        if (bdd_line_is_anim_table_label(line))
            break;   /* proc code ends where its a_* data table begins */
        int x = 0, y = 0;
        if (bdd_movi_a4_coord(line, &x, &y) && n < max) {
            xs[n] = x;
            ys[n] = y;
            n++;
        }
    }
    fclose(f);
    return n;
}

/* Find the proc's x-velocity: the first "movi >v,a0" / "movi ->v,a0" whose
   magnitude is velocity-like (>= 0x1000), which get_bat_obj stores as oxvel.
   Returns the raw signed 16.16 value, 0 if none (static actor). */
static int bdd_proc_velocity(const char *path, const char *proc,
                             char siblings[][48], int sibling_count)
{
    FILE *f;
    char line[512];
    int in = 0, lines = 0;
    if (!path || !proc || !proc[0]) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, proc)) in = 1;
            continue;
        }
        if (++lines > 60) break;
        if (bdd_line_is_any_of(line, siblings, sibling_count, proc))
            break;
        if (bdd_line_is_anim_table_label(line))
            break;   /* proc code ends where its a_* data table begins */
        const char *m = strstr(line, "movi");
        const char *semi = strchr(line, ';');
        if (!m || (semi && semi < m)) continue;
        const char *op = m + 4;
        int neg = 0;
        long v;
        char *end = NULL;
        const char *reg;
        while (*op && isspace((unsigned char)*op)) op++;
        if (*op == '-') { neg = 1; op++; }
        if (*op != '>') continue;
        v = strtol(op + 1, &end, 16);
        if (end == op + 1) continue;
        while (*end && *end != ',') end++;
        if (*end != ',') continue;
        reg = end + 1;
        while (*reg && isspace((unsigned char)*reg)) reg++;
        if (!(reg[0] == 'a' && reg[1] == '0' &&
              !(isalnum((unsigned char)reg[2]) || reg[2] == '_')))
            continue;                 /* velocity is loaded into a0 (oxvel) */
        if (v < 0x1000) continue;        /* skip small counts/flags */
        if ((v & 0xFF) != 0) continue;   /* real 16.16 velocities have a clean low byte */
        fclose(f);
        return neg ? -(int)v : (int)v;
    }
    fclose(f);
    return 0;
}

/* Parse "movi baklstN,b4" -> N (the display-list plane an object inserts into). */
static int bdd_extract_baklst_b4(const char *line)
{
    const char *m = strstr(line, "movi");
    const char *bk = strstr(line, "baklst");
    const char *semi = strchr(line, ';');
    const char *comma, *reg;
    int n;
    if (!m || !bk || bk < m) return -1;
    if (semi && semi < bk) return -1;
    comma = strchr(bk, ',');
    if (!comma) return -1;
    reg = comma + 1;
    while (*reg && isspace((unsigned char)*reg)) reg++;
    if (!(reg[0] == 'b' && reg[1] == '4' &&
          !(isalnum((unsigned char)reg[2]) || reg[2] == '_')))
        return -1;
    n = atoi(bk + 6);
    return (n >= 1 && n <= 8) ? n : -1;
}

/* Extract the operand of a callr/calla/jsrp instruction (the called label). */
static int bdd_extract_call_target(const char *line, char *out, size_t outsz)
{
    static const char *const ops[] = { "callr", "calla", "jsrp" };
    const char *semi = strchr(line, ';');
    for (size_t o = 0; o < sizeof ops / sizeof ops[0]; o++) {
        const char *c = strstr(line, ops[o]);
        if (!c || (semi && semi < c)) continue;
        const char *p = c + strlen(ops[o]);
        if (isalnum((unsigned char)*p) || *p == '_') continue; /* part of a longer word */
        while (*p && isspace((unsigned char)*p)) p++;
        size_t n = 0;
        while (p[n] && (isalnum((unsigned char)p[n]) || p[n] == '_') && n + 1 < outsz) {
            out[n] = p[n];
            n++;
        }
        out[n] = '\0';
        return n > 0;
    }
    return 0;
}

/* Scan a routine block for a direct "movi baklstN,b4" (stops at rets/retp or a
   capped line count). Returns N, or -1. */
static int bdd_block_baklst(const char *path, const char *label, int max_lines)
{
    FILE *f;
    char line[512];
    int in = 0, lines = 0;
    if (!path || !label || !label[0]) return -1;
    f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, label)) in = 1;
            continue;
        }
        if (++lines > max_lines) break;
        int n = bdd_extract_baklst_b4(line);
        if (n > 0) { fclose(f); return n; }
        if (bdd_bgnd_asm_active_directive(line, "rets") ||
            bdd_bgnd_asm_active_directive(line, "retp"))
            break;
    }
    fclose(f);
    return -1;
}

/* Find the baklst plane the proc inserts its object into: a direct "movi
   baklstN,b4" in the proc body, else one inside a helper the proc calls
   (callr/calla/jsrp), e.g. get_bat_obj / make_a_mad_tree. Returns N or -1. */
static int bdd_proc_insert_baklst(const char *path, const char *proc,
                                  char siblings[][48], int sibling_count)
{
    FILE *f;
    char line[512];
    int in = 0, lines = 0;
    char helpers[8][48];
    int nh = 0;
    if (!path || !proc || !proc[0]) return -1;
    f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        if (!in) {
            if (bdd_line_is_label(line, proc)) in = 1;
            continue;
        }
        if (++lines > 120) break;
        if (bdd_line_is_any_of(line, siblings, sibling_count, proc))
            break;
        if (bdd_line_is_anim_table_label(line))
            break;   /* proc code ends where its a_* data table begins */
        int n = bdd_extract_baklst_b4(line);
        if (n > 0) { fclose(f); return n; }
        char tok[48];
        if (nh < (int)(sizeof helpers / sizeof helpers[0]) &&
            bdd_extract_call_target(line, tok, sizeof tok)) {
            int dup = 0;
            for (int j = 0; j < nh; j++)
                if (strcasecmp(helpers[j], tok) == 0) { dup = 1; break; }
            if (!dup) snprintf(helpers[nh++], 48, "%s", tok);
        }
    }
    fclose(f);
    /* Follow the called helpers one level. */
    for (int i = 0; i < nh; i++) {
        int n = bdd_block_baklst(path, helpers[i], 40);
        if (n > 0) return n;
    }
    return -1;
}

/* Public: derive the loaded stage's background animation actors from BGND.ASM:
   each calla pid_bani proc that drives an a_* frame sequence. */
int bdd_stage_runtime_actors(BddStageActor *out, int max_actors)
{
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    char procs[BDD_STAGE_ACTOR_MAX][48];
    int np, n = 0;
    if (!out || max_actors <= 0 || !table || !table->calla_label[0] ||
        !table->source_path[0])
        return 0;

    np = bdd_collect_pid_bani_procs(table->source_path, table->calla_label,
                                    procs, BDD_STAGE_ACTOR_MAX);
    for (int i = 0; i < np && n < max_actors; i++) {
        char seq[48] = "";
        if (!bdd_proc_first_sequence(table->source_path, procs[i], seq, sizeof seq,
                                     procs, np))
            continue;   /* proc drives no a_* frame table */
        snprintf(out[n].proc, sizeof out[n].proc, "%s", procs[i]);
        snprintf(out[n].sequence, sizeof out[n].sequence, "%s", seq);
        out[n].frame_count = bdd_parse_anim_sequence(table->source_path, seq,
                                                     out[n].frames,
                                                     BDD_STAGE_ACTOR_FRAME_MAX);
        out[n].pos_count = bdd_proc_positions(table->source_path, procs[i],
                                              procs, np,
                                              out[n].pos_x, out[n].pos_y,
                                              BDD_STAGE_ACTOR_POS_MAX);
        out[n].motion_x = bdd_proc_velocity(table->source_path, procs[i], procs, np);
        out[n].motion_y = 0;
        out[n].insert_baklst = bdd_proc_insert_baklst(table->source_path, procs[i],
                                                      procs, np);
        /* Movers use worldtlx-relative coords -> screen-fixed; static actors use
           their insertion plane's parallax factor. */
        out[n].screen_anchored = (out[n].motion_x != 0 || out[n].motion_y != 0) ? 1 : 0;
        if (out[n].screen_anchored)
            out[n].scroll = 0.0f;
        else if (out[n].insert_baklst >= 0 &&
                 out[n].insert_baklst < BDD_BGND_SCROLL_ENTRIES)
            out[n].scroll = table->baklst_scroll[out[n].insert_baklst];
        else
            out[n].scroll = 1.0f;
        n++;
    }
    return n;
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
        const BddStageModuleTable *table = bdd_get_stage_module_table();
        int scroll_left = 0;
        int scroll_right = 0;
        int have_limits = 0;
        if (table && table->limits_valid) {
            scroll_left = table->scroll_left;
            scroll_right = table->scroll_right;
            have_limits = 1;
        } else if (label && bdd_read_bgnd_stage_scroll_limits(label, &scroll_left, &scroll_right)) {
            have_limits = 1;
        }
        if (have_limits) {
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

    {
        const BddStageModuleTable *table = bdd_get_stage_module_table();
        if (table && table->camera_valid) {
            x = table->camera_x;
            y = table->camera_y;
        } else if (!bdd_read_bgnd_stage_start_camera(label, &x, &y)) {
            return 0;
        }
    }

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
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    if (table && table->floor_label[0])
        return bdd_object_image_label_equals(obj_index, table->floor_label);
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

/* Per-scanline horizontal shear of the floor, in source pixels: the game's
   skew_calla accumulates skew_dx = -(camera displacement) >> N, so floor line i
   leans by i*shear. 0 when the stage floor has no skew or the camera is at the
   start. */
int bdd_runtime_floor_shear_per_line(void)
{
    const BddStageModuleTable *table = bdd_get_stage_module_table();
    int start_x = 0, start_y = 0;
    if (!table || table->floor_skew_shift <= 0)
        return 0;
    if (!bdd_get_stage_start_camera(&start_x, &start_y))
        return 0;
    int dx = g_scroll_pos - start_x;
    return -(dx >> table->floor_skew_shift);
}

int bdd_object_game_screen_y(int obj_index, int game_y)
{
    if (bdd_object_uses_runtime_floor_y(obj_index))
        return bdd_runtime_floor_screen_y(game_y);
    return game_y - g_game_view_y;
}

/* Draw rank follows the BGND display-list plane order for runtime preview:
   far background first, floor at its -1/floor_code slot, foreground later.
   Background plane and floor ranks are derived from dlists_<stage> for every
   stage. Tower keeps hand-tuned ranks because its clouds/monk/statue objects
   interleave with the background in a way the BGND display list alone does not
   express; Battle's loose props share a fixed mid-ground tier. */
int bdd_object_runtime_draw_rank(int obj_index)
{
    int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
    char module_name[64];

    if (obj_index < 0 || obj_index >= g_no)
        return 1000000;

    /* Tower: hand-tuned interleaving of gameplay objects with the background. */
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
        return 500 + obj_index;
    }

    /* Generic: background plane order straight from dlists_<stage>. */
    if (bdd_object_module_info(obj_index, module_name, (int)sizeof module_name,
                               &mx1, &mx2, &my1, &my2)) {
        int rank = bdd_stage_module_draw_rank(module_name);
        if (rank >= 0)
            return rank;
    }

    /* Battle loose props share the mid-ground tier (BAT2/baklst3 slot). */
    if (bdd_current_stage_is_battle()) {
        static const char *const battle_props[] = {
            "RUBLE1", "BURN_VDA", "SKULLS", "SKELTS",
            "ROCK_VDA", "BURN6_VDA", "RUBLE2"
        };
        if (bdd_object_image_label_is_any(obj_index, battle_props,
                                          (int)(sizeof battle_props / sizeof battle_props[0])))
            return 50;
    }

    if (bdd_object_is_stage_floor(obj_index)) {
        int rank = bdd_stage_floor_draw_rank();
        return rank >= 0 ? rank : 60;
    }

    return 500 + obj_index;
}
