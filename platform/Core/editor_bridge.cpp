#include "bg_editor.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "Core/image_processing.h"
#include "Core/world_module_utils.h"
#include "undo_manager.h"

#include <cstdlib>
#include <cstring>
void bg_editor_undo_save(void)
{
    undo_save();
}

void bg_editor_set_action_label(const char *label)
{
    undo_set_action_label(label);
}

int bg_editor_snap_dist(void)
{
    return g_pref_snap_dist;
}

int bg_editor_visible_pixel_snap_enabled(void)
{
    return g_snap_visible_pixels ? 1 : 0;
}

int bg_editor_object_snap_rect_at(int obj_index, int origin_x, int origin_y,
                                  int *x1, int *y1, int *x2, int *y2)
{
    if (obj_index < 0 || obj_index >= g_no) return 0;
    Obj *o = &g_obj[obj_index];
    Img *im = img_find(o->ii);
    if (!im || im->w <= 0 || im->h <= 0) return 0;

    int rx1 = 0;
    int ry1 = 0;
    int rx2 = im->w - 1;
    int ry2 = im->h - 1;
    if (g_snap_visible_pixels) {
        int vx1 = 0, vy1 = 0, vx2 = 0, vy2 = 0;
        if (image_nonzero_bounds(im, &vx1, &vy1, &vx2, &vy2)) {
            rx1 = o->hfl ? (im->w - 1 - vx2) : vx1;
            rx2 = o->hfl ? (im->w - 1 - vx1) : vx2;
            ry1 = o->vfl ? (im->h - 1 - vy2) : vy1;
            ry2 = o->vfl ? (im->h - 1 - vy1) : vy2;
        }
    }

    if (x1) *x1 = origin_x + rx1;
    if (y1) *y1 = origin_y + ry1;
    if (x2) *x2 = origin_x + rx2 + 1;
    if (y2) *y2 = origin_y + ry2 + 1;
    return 1;
}

int bg_editor_object_hit_test_at(int obj_index, int origin_x, int origin_y,
                                 int x, int y)
{
    if (obj_index < 0 || obj_index >= g_no) return 0;
    Obj *o = &g_obj[obj_index];
    Img *im = img_find(o->ii);
    if (!im || im->w <= 0 || im->h <= 0) return 0;

    int lx = x - origin_x;
    int ly = y - origin_y;
    if (lx < 0 || ly < 0 || lx >= im->w || ly >= im->h)
        return 0;
    if (!im->pix)
        return 1;

    /* Picking should always respect transparent pixels. g_snap_visible_pixels
       controls edge snapping only; tying selection to it makes large transparent
       sprites block smaller objects behind them. */
    int sx = o->hfl ? (im->w - 1 - lx) : lx;
    int sy = o->vfl ? (im->h - 1 - ly) : ly;
    return im->pix[(size_t)sy * (size_t)im->w + (size_t)sx] != 0;
}

void bg_editor_place_last_import(int world_x, int world_y)
{
    if (g_last_import_img < 0 || g_last_import_img >= g_ni) return;
    if (!editor_project_reserve_objects(g_no + 1)) return;
    undo_save_ex("Place Sprite");
    Img *im = &g_img[g_last_import_img];
    Obj *o = editor_project_append_object_slot();
    if (!o) return;
    int obj_i = g_no - 1;
    o->wx = 0x4100; o->depth = world_x; o->sy = world_y;
    o->ii = im->idx; o->fl = (im->pal_idx >= 0) ? im->pal_idx : 0;
    o->hfl = 0; o->vfl = 0; o->order = obj_i;
    g_hl_obj = obj_i; g_sel_flags[obj_i] = 1;
    simple_ensure_module(g_hl_obj);
    g_dirty = 1; g_need_rebuild = 1;
}

