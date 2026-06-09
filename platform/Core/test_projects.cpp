#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "UI/toast_notifications.h"
#include "undo_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void proof_rect(Uint8 *pix, int w, int h, int x, int y, int rw, int rh, Uint8 color)
{
    int x2 = x + rw;
    int y2 = y + rh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > w) x2 = w;
    if (y2 > h) y2 = h;
    for (int yy = y; yy < y2; yy++)
        for (int xx = x; xx < x2; xx++)
            pix[yy * w + xx] = color;
}

static const char *proof_glyph_row(char c, int row)
{
    static const char *blank[7] = {
        "00000", "00000", "00000", "00000", "00000", "00000", "00000"
    };
    static const char *b[7] = {
        "11110", "10001", "10001", "11110", "10001", "10001", "11110"
    };
    static const char *g[7] = {
        "01111", "10000", "10000", "10111", "10001", "10001", "01111"
    };
    static const char *p_glyph[7] = {
        "11110", "10001", "10001", "11110", "10000", "10000", "10000"
    };
    static const char *r[7] = {
        "11110", "10001", "10001", "11110", "10100", "10010", "10001"
    };
    static const char *o[7] = {
        "01110", "10001", "10001", "10001", "10001", "10001", "01110"
    };
    static const char *f[7] = {
        "11111", "10000", "10000", "11110", "10000", "10000", "10000"
    };
    static const char *t[7] = {
        "11111", "00100", "00100", "00100", "00100", "00100", "00100"
    };
    static const char *s[7] = {
        "01111", "10000", "10000", "01110", "00001", "00001", "11110"
    };
    static const char *one[7] = {
        "00100", "01100", "00100", "00100", "00100", "00100", "01110"
    };
    const char **glyph = blank;
    if (c == 'B') glyph = b;
    else if (c == 'G') glyph = g;
    else if (c == 'P') glyph = p_glyph;
    else if (c == 'R') glyph = r;
    else if (c == 'O') glyph = o;
    else if (c == 'F') glyph = f;
    else if (c == 'T') glyph = t;
    else if (c == 'S') glyph = s;
    else if (c == '1') glyph = one;
    return glyph[row];
}

static void proof_text(Uint8 *pix, int w, int h, int x, int y,
                       const char *text, int scale, Uint8 color)
{
    int cursor = x;
    for (const char *p = text; *p; p++) {
        for (int row = 0; row < 7; row++) {
            const char *bits = proof_glyph_row(*p, row);
            for (int col = 0; col < 5; col++) {
                if (bits[col] == '1')
                    proof_rect(pix, w, h, cursor + col * scale, y + row * scale,
                               scale, scale, color);
            }
        }
        cursor += 6 * scale;
    }
}

static void proof_mark_tiles(Uint8 *pix, int w, int h, int tile_w, int tile_h)
{
    int cols = w / tile_w;
    int rows = (h + tile_h - 1) / tile_h;

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int idx = row * cols + col + 1;
            int x0 = col * tile_w;
            int y0 = row * tile_h;
            int cur_h = tile_h;
            if (y0 + cur_h > h) cur_h = h - y0;

            int mark_w = 10;
            int mark_h = 8;
            int mx = x0 + 4;
            int my = y0 + cur_h - mark_h - 1;
            if (mx + mark_w > x0 + tile_w) mx = x0;
            if (my < y0) my = y0;

            /* Deterministic per-tile signature: unique checksum and max pixel 8. */
            for (int yy = 0; yy < mark_h; yy++)
                for (int xx = 0; xx < mark_w; xx++)
                    pix[(my + yy) * w + (mx + xx)] = 1;
            for (int k = 0; k < idx && k < mark_w * mark_h; k++)
                pix[(my + k / mark_w) * w + (mx + k % mark_w)] = 8;
        }
    }
}

static void fill_bg_proof_pixels(Uint8 *pix, int w, int h)
{
    static const Uint8 bars[7] = {1, 2, 3, 4, 6, 7, 5};

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int bi = (x * 7) / w;
            pix[y * w + x] = bars[bi < 0 ? 0 : (bi > 6 ? 6 : bi)];
        }
    }

    proof_rect(pix, w, h, 0, 0, 32, 32, 1);
    proof_rect(pix, w, h, w - 32, 0, 32, 32, 2);
    proof_rect(pix, w, h, 0, h - 32, 32, 32, 3);
    proof_rect(pix, w, h, w - 32, h - 32, 32, 32, 4);

    for (int y = 36; y < h; y += 24)
        proof_rect(pix, w, h, 0, y, w, 3, 5);
    for (int x = 36; x < w; x += 40)
        proof_rect(pix, w, h, x, 0, 3, h, 8);

    proof_rect(pix, w, h, 36, 84, 328, 86, 8);
    proof_rect(pix, w, h, 42, 90, 316, 74, 6);
    proof_rect(pix, w, h, 50, 98, 300, 58, 8);
    proof_text(pix, w, h, 62, 100, "BGPROF", 8, 5);

    proof_rect(pix, w, h, 0, 0, w, 4, 5);
    proof_rect(pix, w, h, 0, h - 4, w, 4, 5);
    proof_rect(pix, w, h, 0, 0, 4, h, 5);
    proof_rect(pix, w, h, w - 4, 0, 4, h, 5);

    proof_mark_tiles(pix, w, h, 40, 32);
}

void create_bg_proof_level(void)
{
    const int proof_w = 400;
    const int proof_h = 254;
    const int proof_tile_w = 40;
    const int proof_tile_h = 32;
    const int proof_cols = proof_w / proof_tile_w;
    const int proof_rows = (proof_h + proof_tile_h - 1) / proof_tile_h;
    const int proof_tiles = proof_cols * proof_rows;

    undo_save();
    if (!editor_project_reserve_images(proof_tiles) ||
        !editor_project_reserve_objects(proof_tiles) ||
        !editor_project_reserve_modules(1) ||
        !editor_project_reserve_palettes(1)) {
        stage_set_toast("Could not allocate BGPROF project storage");
        return;
    }
    editor_project_reset_loaded_stage();

    snprintf(g_name, sizeof g_name, "BGPROF");
    snprintf(g_bdb_header, sizeof g_bdb_header, "BGPROF 400 254 255 1 1 %d", proof_tiles);
    editor_project_set_single_module_line("TSTMOD 0 399 0 253");
    g_have_bdb = 1;

    Uint8 *full = (Uint8 *)malloc((size_t)proof_w * (size_t)proof_h);
    if (full)
        fill_bg_proof_pixels(full, proof_w, proof_h);

    for (int i = 0; i < proof_tiles; i++) {
        int row = i / proof_cols;
        int col = i % proof_cols;
        int tile_h = proof_tile_h;
        int y0 = row * proof_tile_h;
        int x0 = col * proof_tile_w;
        if (y0 + tile_h > proof_h) tile_h = proof_h - y0;
        Img *im = editor_project_append_image_slot();
        if (!im) break;
        im->idx = i + 1;
        im->w = proof_tile_w;
        im->h = tile_h;
        im->flags = 0;
        im->pal_idx = 0;
        im->pix = (Uint8 *)malloc((size_t)proof_tile_w * (size_t)tile_h);
        if (im->pix && full) {
            for (int y = 0; y < tile_h; y++) {
                memcpy(im->pix + (size_t)y * (size_t)proof_tile_w,
                       full + (size_t)(y0 + y) * (size_t)proof_w + (size_t)x0,
                       (size_t)proof_tile_w);
            }
        }
    }
    free(full);

    Uint32 proof_pal[256] = {};
    proof_pal[0] = 0x00000000u;
    proof_pal[1] = 0xFFFF0000u;
    proof_pal[2] = 0xFF00FF00u;
    proof_pal[3] = 0xFF0000FFu;
    proof_pal[4] = 0xFFFFFF00u;
    proof_pal[5] = 0xFFFFFFFFu;
    proof_pal[6] = 0xFFFF00FFu;
    proof_pal[7] = 0xFF00FFFFu;
    proof_pal[8] = 0xFF000000u;
    editor_project_append_palette_slot("PROOFPAL", 9, proof_pal);
    g_sel_pal = 0;

    int obj_i = 0;
    for (int col = 0; col < proof_cols; col++) {
        for (int row = 0; row < proof_rows; row++) {
            int img_idx = row * proof_cols + col + 1;
            int i = obj_i++;
            Obj *o = editor_project_append_object_slot();
            if (!o) continue;
            o->wx = 0x4000;
            o->depth = col * proof_tile_w;
            o->sy = row * proof_tile_h;
            o->ii = img_idx;
            o->fl = 0;
            o->hfl = 0;
            o->vfl = 0;
            o->order = i;
        }
    }
    g_sel_flags[0] = 1;
    g_hl_obj = 0;

    snprintf(g_bdb_path, sizeof g_bdb_path, "BGPROF.BDB");
    snprintf(g_bdd_path, sizeof g_bdd_path, "BGPROF.BDD");
    g_tile_img = 0;
    g_tile_layer = 0x40;
    g_tile_cols = 1;
    g_tile_rows = 1;
    g_tile_sx = proof_tile_w;
    g_tile_sy = proof_tile_h;
    g_tile_ox = 0;
    g_tile_oy = 0;
    g_view_x = 0;
    g_view_y = 0;
    g_zoom = 1;
    g_view_changed = 1;
    g_need_rebuild = 1;
    g_dirty = 1;
    g_show_images = true;
    g_show_mk2_workflow = true;
    snprintf(g_toast_msg, sizeof g_toast_msg, "Created BGPROF full-screen proof");
    g_toast_timer = 3.0f;
}

void create_checker_test_level(void)
{
    undo_save();
    if (!editor_project_reserve_images(1) ||
        !editor_project_reserve_objects(1) ||
        !editor_project_reserve_modules(1) ||
        !editor_project_reserve_palettes(1)) {
        stage_set_toast("Could not allocate CHECKER project storage");
        return;
    }
    editor_project_reset_loaded_stage();

    snprintf(g_name, sizeof g_name, "CHECKER");
    snprintf(g_bdb_header, sizeof g_bdb_header, "CHECKER 400 254 255 1 1 1");
    editor_project_set_single_module_line("CHKMOD 0 399 0 253");
    g_have_bdb = 1;

    Img *im = editor_project_append_image_slot();
    if (!im) return;
    im->idx = 0x01;
    im->w = 64;
    im->h = 64;
    im->flags = 0;
    im->pal_idx = 0;
    im->pix = (Uint8 *)malloc(64 * 64);
    if (im->pix) {
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                im->pix[y * 64 + x] = (Uint8)(1 + (((x / 8) + (y / 8)) & 3));
            }
        }
    }

    Uint32 check_pal[256] = {};
    check_pal[0] = 0x00000000u;
    check_pal[1] = 0xFFFF0000u;
    check_pal[2] = 0xFF00FF00u;
    check_pal[3] = 0xFF0000FFu;
    check_pal[4] = 0xFFFFFF00u;
    editor_project_append_palette_slot("CHECKPAL", 5, check_pal);
    g_sel_pal = 0;

    Obj *o = editor_project_append_object_slot();
    if (!o) return;
    o->wx = 0x4000;
    o->depth = 0;
    o->sy = 0;
    o->ii = 0x01;
    o->fl = 0;
    o->hfl = 0;
    o->vfl = 0;
    o->order = 0;
    g_sel_flags[0] = 1;
    g_hl_obj = 0;

    snprintf(g_bdb_path, sizeof g_bdb_path, "CHECKER.BDB");
    snprintf(g_bdd_path, sizeof g_bdd_path, "CHECKER.BDD");
    g_tile_img = 0;
    g_tile_layer = 0x40;
    g_tile_cols = 1;
    g_tile_rows = 1;
    g_tile_sx = 64;
    g_tile_sy = 64;
    g_tile_ox = 0;
    g_tile_oy = 0;
    g_view_x = 0;
    g_view_y = 0;
    g_zoom = 2;
    g_view_changed = 1;
    g_need_rebuild = 1;
    g_dirty = 1;
    g_show_images = true;
    g_show_mk2_workflow = true;
    snprintf(g_toast_msg, sizeof g_toast_msg, "Created CHECKER smoke test");
    g_toast_timer = 3.0f;
}

