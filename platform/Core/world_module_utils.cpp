#include "Core/world_module_utils.h"

#include "bg_editor.h"
#include "Core/bdd_core.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/project_header.h"

#include <climits>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define module_name_strcasecmp _stricmp
#else
#include <strings.h>
#define module_name_strcasecmp strcasecmp
#endif

bool module_name_in_use(const char *name, int except_module_idx)
{
    if (!name || !name[0]) return false;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        if (m == except_module_idx) continue;
        char mn[64] = "";
        if (sscanf(g_bdb_modules[m], "%63s", mn) == 1 &&
            module_name_strcasecmp(mn, name) == 0)
            return true;
    }
    return false;
}

/* Stage-name-prefixed so an auto-generated name (e.g. "MOD0") can't collide
 * with another stage's module of the same generic name once promoted into
 * the shared BGND.ASM -- collisions there mean two modules fighting over one
 * <name>BMOD symbol. Also guaranteed unique against every module already in
 * this stage, so renaming/recreating can never quietly produce a duplicate
 * like two modules both ending up named the same thing. */
void module_generate_unique_name(char *out, size_t outsz, const char *base)
{
    char stage_prefix[16] = "";
    if (g_name[0]) {
        size_t n = 0;
        for (const char *p = g_name; *p && n < sizeof(stage_prefix) - 1; p++, n++)
            stage_prefix[n] = *p;
        stage_prefix[n] = '\0';
    }
    const char *base_name = (base && base[0]) ? base : "MOD";
    for (int n = 0; n < 10000; n++) {
        char candidate[64];
        if (stage_prefix[0])
            snprintf(candidate, sizeof candidate, "%s_%s%d", stage_prefix, base_name, n);
        else
            snprintf(candidate, sizeof candidate, "%s%d", base_name, n);
        if (!module_name_in_use(candidate, -1)) {
            snprintf(out, outsz, "%s", candidate);
            return;
        }
    }
    snprintf(out, outsz, "%s", base_name);
}

#define MODULE_LOCK_MAX 256
static char g_locked_module_names[MODULE_LOCK_MAX][64];
static int g_locked_module_count = 0;

bool module_is_locked(const char *name)
{
    if (!name || !name[0]) return false;
    for (int i = 0; i < g_locked_module_count; i++)
        if (module_name_strcasecmp(g_locked_module_names[i], name) == 0)
            return true;
    return false;
}

void module_set_locked(const char *name, bool locked)
{
    if (!name || !name[0]) return;
    for (int i = 0; i < g_locked_module_count; i++) {
        if (module_name_strcasecmp(g_locked_module_names[i], name) == 0) {
            if (!locked) {
                g_locked_module_count--;
                if (i != g_locked_module_count)
                    memcpy(g_locked_module_names[i], g_locked_module_names[g_locked_module_count],
                           sizeof g_locked_module_names[0]);
            }
            return;
        }
    }
    if (locked && g_locked_module_count < MODULE_LOCK_MAX) {
        snprintf(g_locked_module_names[g_locked_module_count],
                 sizeof g_locked_module_names[0], "%s", name);
        g_locked_module_count++;
    }
}

bool module_is_locked_by_index(int module_idx)
{
    char mn[64] = "";
    if (module_idx < 0 || module_idx >= g_bdb_num_modules) return false;
    if (sscanf(g_bdb_modules[module_idx], "%63s", mn) != 1) return false;
    return module_is_locked(mn);
}

int module_smallest_containing(int depth, int sy, int width, int height)
{
    int best = -1;
    long best_area = 0;
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;
    int x2 = depth + width - 1;
    int y2 = sy + height - 1;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
        if (!parse_module_bounds(m, NULL, &mx1, &mx2, &my1, &my2)) continue;
        if (mx2 < mx1 || my2 < my1) continue;
        if (depth < mx1 || x2 > mx2 || sy < my1 || y2 > my2) continue;
        long area = (long)(mx2 - mx1 + 1) * (long)(my2 - my1 + 1);
        if (best < 0 || area < best_area) {
            best = m;
            best_area = area;
        }
    }
    return best;
}

bool object_in_locked_module(int obj_index)
{
    if (obj_index < 0 || obj_index >= g_no) return false;
    Img *im = img_find(g_obj[obj_index].ii);
    if (!im) return false;
    /* Smallest-containing, not assign_module's LOAD2 first-fit: a locked
     * outer/catch-all module must not also lock down objects that visually
     * sit inside a smaller, unlocked module nested or overlapping inside it
     * -- otherwise locking the big module makes everything nested in it
     * un-draggable too, including objects meant for the smaller module. */
    int m = module_smallest_containing(g_obj[obj_index].depth, g_obj[obj_index].sy, im->w, im->h);
    if (m < 0) return false;
    return module_is_locked_by_index(m);
}

int get_world_size(int *out_w, int *out_h)
{
    char nm[64] = "";
    int ww = 0, wh = 0, md = 0, nmod = 0, np = 0, no = 0;
    if (g_bdb_header[0] &&
        sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
               nm, &ww, &wh, &md, &nmod, &np, &no) >= 7 &&
        ww > 0 && wh > 0) {
        if (out_w) *out_w = ww;
        if (out_h) *out_h = wh;
        return 1;
    }
    return 0;
}

void fit_tile_to_world(void)
{
    if (g_tile_img < 0 || g_tile_img >= g_ni) return;
    Img *im = &g_img[g_tile_img];
    if (!im->w || !im->h) return;
    int ww = 0, wh = 0;
    if (!get_world_size(&ww, &wh)) return;
    g_tile_sx = im->w;
    g_tile_sy = im->h;
    g_tile_cols = (ww + im->w - 1) / im->w;
    g_tile_rows = (wh + im->h - 1) / im->h;
    g_tile_ox = 0;
    g_tile_oy = 0;
}

int parse_module_bounds(int m, char *name, int *x1, int *x2, int *y1, int *y2)
{
    if (m < 0 || m >= g_bdb_num_modules) return 0;
    BddCoreModule module;
    if (!bdd_core_parse_module_line(g_bdb_modules[m], &module))
        return 0;
    if (name) snprintf(name, 64, "%s", module.name);
    if (x1) *x1 = module.x1;
    if (x2) *x2 = module.x2;
    if (y1) *y1 = module.y1;
    if (y2) *y2 = module.y2;
    return 1;
}

int image_max_pixel(const Img *im)
{
    if (!im) return 0;
    return bdd_core_image_max_pixel(im->pix, im->w, im->h);
}

int assign_module(int depth, int sy, int width, int height)
{
    return bdd_core_find_fitting_module_in_lines(
        (const char (*)[256])g_bdb_modules,
        g_bdb_num_modules,
        depth,
        sy,
        width,
        height,
        NULL);
}

/* In Simple mode, silently ensure the object at g_obj[obj_idx] fits inside
   a module.  If none contains it, expand the nearest module's bounds, or
   create a catch-all module when there are none at all. */
void simple_ensure_module(int obj_idx)
{
    if (!g_simple_mode || !g_have_bdb) return;
    if (obj_idx < 0 || obj_idx >= g_no) return;
    Obj *o = &g_obj[obj_idx];
    Img *im = img_find(o->ii);
    int ow = im ? im->w : 1;
    int oh = im ? im->h : 1;

    if (assign_module(o->depth, o->sy, ow, oh) >= 0)
        return; /* already fits */

    if (g_bdb_num_modules == 0) {
        /* create a world-spanning catch-all module */
        int wx_min=0, wx_max=1024, wy_min=0, wy_max=256;
        bdd_get_world_bounds(&wx_min, &wx_max, &wy_min, &wy_max);
        char line[256];
        snprintf(line, sizeof line, "MOD0 %d %d %d %d",
                 wx_min, wx_max, wy_min, wy_max);
        editor_project_set_single_module_line(line);
        sync_bdb_header_counts();
        g_dirty = 1;
        return;
    }

    /* find the module whose centre is closest to this object, then expand it */
    int best = 0, best_dist = INT_MAX;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        char mn[64]; int x1=0,x2=0,y1=0,y2=0;
        if (!parse_module_bounds(m, mn, &x1, &x2, &y1, &y2)) continue;
        int cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
        int dx = o->depth - cx, dy = o->sy - cy;
        int d = dx*dx + dy*dy;
        if (d < best_dist) { best_dist = d; best = m; }
    }
    char mn[64]; int x1=0,x2=0,y1=0,y2=0;
    if (!parse_module_bounds(best, mn, &x1, &x2, &y1, &y2)) return;
    if (o->depth       < x1) x1 = o->depth;
    if (o->sy          < y1) y1 = o->sy;
    if (o->depth+ow-1  > x2) x2 = o->depth + ow - 1;
    if (o->sy   +oh-1  > y2) y2 = o->sy    + oh - 1;
    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d", mn, x1, x2, y1, y2);
    if (editor_project_set_module_line(best, line))
        g_dirty = 1;
}
