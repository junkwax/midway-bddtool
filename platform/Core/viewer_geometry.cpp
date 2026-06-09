#include "bg_editor.h"

#include "Core/bdd_core.h"
#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"

#include <climits>
#include <cstdio>
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

static int bdd_current_stage_is_battle(void)
{
    if (g_name[0] && strcasecmp(g_name, "BATTLE") == 0)
        return 1;
    return strstr(g_bdb_path, "BATTLE") || strstr(g_bdb_path, "battle") ||
           strstr(g_bdd_path, "BATTLE") || strstr(g_bdd_path, "battle");
}

static int bdd_battle_module_runtime_info(const char *name, int *ox, int *oy, float *scroll)
{
    if (!name || !bdd_current_stage_is_battle())
        return 0;

    if (strcasecmp(name, "BAT1") == 0 || strcasecmp(name, "BAT1BMOD") == 0) {
        if (ox) *ox = 0;   if (oy) *oy = 0x93; if (scroll) *scroll = 0.8125f; return 1;
    }
    if (strcasecmp(name, "BAT2") == 0 || strcasecmp(name, "BAT2BMOD") == 0) {
        if (ox) *ox = 249; if (oy) *oy = 0x04; if (scroll) *scroll = 0.5f; return 1;
    }
    if (strcasecmp(name, "BAT4") == 0 || strcasecmp(name, "BAT4BMOD") == 0) {
        if (ox) *ox = 222; if (oy) *oy = 0x61; if (scroll) *scroll = 0.25f; return 1;
    }
    if (strcasecmp(name, "BAT5") == 0 || strcasecmp(name, "BAT5BMOD") == 0) {
        if (ox) *ox = 667; if (oy) *oy = 0x40; if (scroll) *scroll = 0.09375f; return 1;
    }
    if (strcasecmp(name, "BAT6") == 0 || strcasecmp(name, "BAT6BMOD") == 0) {
        if (ox) *ox = 401; if (oy) *oy = 0x5a; if (scroll) *scroll = 0.0625f; return 1;
    }
    if (strcasecmp(name, "BAT7") == 0 || strcasecmp(name, "BAT7BMOD") == 0) {
        if (ox) *ox = 424; if (oy) *oy = 0x1a; if (scroll) *scroll = 0.0f; return 1;
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
        if (bdd_battle_module_runtime_info(module_name, &ox, &oy, NULL)) {
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
        if (bdd_battle_module_runtime_info(module_name, &ox, &oy, NULL)) {
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
    /* Authentic BATTLE-stage modules keep their known runtime scroll rates. */
    if (bdd_object_module_info(obj_index, module_name, (int)sizeof module_name, &mx1, &mx2, &my1, &my2) &&
        bdd_battle_module_runtime_info(module_name, NULL, NULL, &scroll))
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
