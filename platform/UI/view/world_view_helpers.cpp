#include "bg_editor.h"
#include "Core/bdd_core.h"
#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"
#include "UI/view/world_view_helpers.h"

float gv_scroll_factor(int layer_byte)
{
    return bdd_core_mk2_scroll_factor(layer_byte);
}

void gv_object_origin(int obj_index, int *x, int *y)
{
    if (g_runtime_layout_view && bdd_object_runtime_origin(obj_index, x, y))
        return;
    if (obj_index >= 0 && obj_index < g_no) {
        if (x) *x = g_obj[obj_index].depth;
        if (y) *y = g_obj[obj_index].sy;
    }
}

float mk2_scroll_factor_for_layer(int layer)
{
    return gv_scroll_factor(layer);
}

static int screen_rect_intersects_clip(const BddScreenRect *rect)
{
    if (!rect) return 0;
    if (rect->x + rect->w < rect->clip_x || rect->x > rect->clip_x + rect->clip_w)
        return 0;
    if (rect->y + rect->h < rect->clip_y || rect->y > rect->clip_y + rect->clip_h)
        return 0;
    return 1;
}

void bdd_game_view_screen_rect(int zoom, int window_w, int window_h,
                               BddScreenRect *out_rect)
{
    if (!out_rect) return;
    BddScreenRect rect = {};
    if (zoom > 0) {
        rect.w = 400 * zoom;
        rect.h = 254 * zoom;
    }
    int canvas_top = bg_editor_canvas_top_px();
    int bottom_reserved = g_preview_mode ? 0 : 152;
    int usable_h = window_h - canvas_top - bottom_reserved;
    if (usable_h < rect.h) usable_h = rect.h;
    rect.x = (window_w - rect.w) / 2;
    rect.y = canvas_top + (usable_h - rect.h) / 2;
    if (rect.y < canvas_top) rect.y = canvas_top;
    rect.clip_x = rect.x;
    rect.clip_y = rect.y;
    rect.clip_w = rect.w;
    rect.clip_h = rect.h;
    *out_rect = rect;
}

void bdd_world_to_screen(int world_x, int world_y,
                         int view_x, int view_y, int zoom,
                         int *screen_x, int *screen_y)
{
    if (screen_x) *screen_x = (world_x - view_x) * zoom;
    if (screen_y) *screen_y = (world_y - view_y) * zoom;
}

void bdd_screen_to_world(int screen_x, int screen_y,
                         int view_x, int view_y, int zoom,
                         int *world_x, int *world_y)
{
    if (zoom <= 0) {
        if (world_x) *world_x = view_x;
        if (world_y) *world_y = view_y;
        return;
    }
    if (world_x) *world_x = screen_x / zoom + view_x;
    if (world_y) *world_y = screen_y / zoom + view_y;
}

int bdd_world_rect_screen_rect(int world_x1, int world_y1,
                               int world_x2, int world_y2,
                               int view_x, int view_y, int zoom,
                               int window_w, int window_h,
                               BddScreenRect *out_rect)
{
    if (!out_rect || world_x2 <= world_x1 || world_y2 <= world_y1 || zoom <= 0)
        return 0;

    BddScreenRect rect = {};
    rect.clip_x = 0;
    rect.clip_y = 0;
    rect.clip_w = window_w;
    rect.clip_h = window_h;
    bdd_world_to_screen(world_x1, world_y1, view_x, view_y, zoom,
                        &rect.x, &rect.y);
    rect.w = (world_x2 - world_x1) * zoom;
    rect.h = (world_y2 - world_y1) * zoom;

    *out_rect = rect;
    return screen_rect_intersects_clip(&rect);
}

int bdd_object_screen_rect(int obj_index, int image_w, int image_h,
                           int view_x, int view_y, int zoom,
                           int window_w, int window_h,
                           int game_scroll, BddScreenRect *out_rect)
{
    if (!out_rect || obj_index < 0 || obj_index >= g_no ||
        image_w <= 0 || image_h <= 0 || zoom <= 0)
        return 0;

    BddScreenRect rect = {};
    rect.clip_x = 0;
    rect.clip_y = 0;
    rect.clip_w = window_w;
    rect.clip_h = window_h;

    if (g_game_view) {
        BddScreenRect viewport;
        bdd_game_view_screen_rect(zoom, window_w, window_h, &viewport);
        rect.clip_x = viewport.x;
        rect.clip_y = viewport.y;
        rect.clip_w = viewport.w;
        rect.clip_h = viewport.h;
    }

    int ox = g_obj[obj_index].depth;
    int oy = g_obj[obj_index].sy;
    if (g_game_view) {
        float sf = bdd_object_game_scroll_factor(obj_index);
        bdd_object_game_origin(obj_index, &ox, &oy);
        rect.x = rect.clip_x + (ox - (int)(game_scroll * sf)) * zoom;
        rect.y = rect.clip_y + (oy - g_game_view_y) * zoom;
    } else {
        bdd_object_editor_origin(obj_index, &ox, &oy);
        rect.x = (ox - view_x) * zoom;
        rect.y = (oy - view_y) * zoom;
    }
    rect.w = image_w * zoom;
    rect.h = image_h * zoom;

    *out_rect = rect;
    return screen_rect_intersects_clip(&rect);
}

int bdd_object_screen_snap_rect(int obj_index,
                                int view_x, int view_y, int zoom,
                                int window_w, int window_h,
                                int game_scroll, BddScreenRect *out_rect)
{
    if (!out_rect || obj_index < 0 || obj_index >= g_no || zoom <= 0)
        return 0;

    Img *im = img_find(g_obj[obj_index].ii);
    if (!im || im->w <= 0 || im->h <= 0)
        return 0;

    BddScreenRect base;
    bdd_object_screen_rect(obj_index, im->w, im->h,
                           view_x, view_y, zoom,
                           window_w, window_h, game_scroll, &base);

    int ox = g_obj[obj_index].depth;
    int oy = g_obj[obj_index].sy;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    BddScreenRect rect = base;
    if (g_game_view) {
        float sf = bdd_object_game_scroll_factor(obj_index);
        bdd_object_game_origin(obj_index, &ox, &oy);
        ox = ox - (int)(game_scroll * sf);
        oy = oy - g_game_view_y;
        if (!bg_editor_object_snap_rect_at(obj_index, ox, oy, &x1, &y1, &x2, &y2))
            return 0;
        rect.x = rect.clip_x + x1 * zoom;
        rect.y = rect.clip_y + y1 * zoom;
    } else {
        bdd_object_editor_origin(obj_index, &ox, &oy);
        if (!bg_editor_object_snap_rect_at(obj_index, ox, oy, &x1, &y1, &x2, &y2))
            return 0;
        rect.x = (x1 - view_x) * zoom;
        rect.y = (y1 - view_y) * zoom;
    }
    rect.w = (x2 - x1) * zoom;
    rect.h = (y2 - y1) * zoom;

    *out_rect = rect;
    return screen_rect_intersects_clip(&rect);
}

int active_image_index(void)
{
    if (g_hl_obj >= 0 && g_hl_obj < g_no) {
        Img *im = img_find(g_obj[g_hl_obj].ii);
        if (im) return (int)(im - g_img);
    }
    if (g_tile_img >= 0 && g_tile_img < g_ni) return g_tile_img;
    return g_ni > 0 ? 0 : -1;
}

int object_pixel_at_screen(const Obj *o, const Img *im, int camera_x, int screen_x, int screen_y)
{
    if (!o || !im || !im->pix) return -1;
    int layer = (o->wx >> 8) & 0xFF;
    int sx = o->depth - (int)(camera_x * mk2_scroll_factor_for_layer(layer));
    int lx = screen_x - sx;
    int ly = screen_y - o->sy;
    if (lx < 0 || ly < 0 || lx >= im->w || ly >= im->h) return -1;
    if (o->hfl) lx = im->w - 1 - lx;
    if (o->vfl) ly = im->h - 1 - ly;
    return im->pix[ly * im->w + lx];
}

int img_anim_offset_x(const Img *im, int hfl)
{
    if (!im) return 0;
    int off = im->anix;
    if (hfl)
        off = (im->w > 0 ? im->w - 1 : 0) - off;
    return off;
}

int img_anim_offset_y(const Img *im, int vfl)
{
    if (!im) return 0;
    int off = im->aniy;
    if (vfl)
        off = (im->h > 0 ? im->h - 1 : 0) - off;
    return off;
}
