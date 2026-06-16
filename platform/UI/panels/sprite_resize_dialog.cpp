#include "bg_editor_globals.h"
#include "UI/assets/image_draw_helpers.h"
#include "imgui.h"
#include "undo_manager.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Shared resize-dialog state. The dialog runs in one of two modes:
   single  - resize one image asset (width/height or percentage), and
   group   - resize every image used by the current object selection by a
             uniform percentage. Both modes share the anchor / duplicate /
             percentage controls and the live preview strip. */
#define RESIZE_GROUP_MAX 256

static bool  g_resize_open     = false;
static bool  g_resize_request  = false;
static bool  g_resize_group    = false;
static int   g_resize_img      = -1;
static int   g_resize_orig_w   = 0;
static int   g_resize_orig_h   = 0;
static int   g_resize_w        = 1;
static int   g_resize_h        = 1;
static int   g_resize_anchor   = 2;
static float g_resize_pct      = 100.0f;
static bool  g_resize_keep_aspect = true;
static bool  g_resize_selected_only = true;
static bool  g_resize_connect  = true;

static int   g_resize_group_slots[RESIZE_GROUP_MAX];
static int   g_resize_group_n   = 0;
static int   g_resize_group_uses = 0;
static int   g_resize_group_skipped = 0;

static int scale_dim(int v, float pct)
{
    int r = (int)((double)v * pct / 100.0 + 0.5);
    if (r < 1) r = 1;
    if (r > 4096) r = 4096;
    return r;
}

void open_sprite_resize(int img_idx, bool selected_only_default)
{
    if (img_idx < 0 || img_idx >= g_ni) return;
    Img *im = &g_img[img_idx];
    if (!im->pix || im->w <= 0 || im->h <= 0) return;
    if (runtime_actor_image_is_preview_import(im)) {
        stage_set_toast("Runtime source art is read-only");
        return;
    }
    g_resize_group = false;
    g_resize_img = img_idx;
    g_resize_orig_w = im->w;
    g_resize_orig_h = im->h;
    g_resize_w = im->w;
    g_resize_h = im->h;
    g_resize_pct = 100.0f;
    g_resize_keep_aspect = true;
    g_resize_anchor = 2;
    g_resize_selected_only = selected_only_default;
    g_resize_open = true;
    g_resize_request = true;
}

/* Collect the unique, editable image slots used by the current selection
   (falling back to the highlighted object) so a whole group of sprites can be
   rescaled in one pass. */
void open_group_sprite_resize(void)
{
    g_resize_group_n = 0;
    g_resize_group_uses = 0;
    g_resize_group_skipped = 0;

    int order[RESIZE_GROUP_MAX];
    int order_n = 0;
    for (int i = 0; i < g_no; i++) {
        if (!g_sel_flags[i]) continue;
        if (order_n < RESIZE_GROUP_MAX) order[order_n++] = i;
    }
    if (order_n == 0 && g_hl_obj >= 0 && g_hl_obj < g_no)
        order[order_n++] = g_hl_obj;

    for (int k = 0; k < order_n; k++) {
        Img *im = img_find(g_obj[order[k]].ii);
        if (!im || !im->pix || im->w <= 0 || im->h <= 0) continue;
        int slot = (int)(im - g_img);
        if (runtime_actor_image_is_preview_import(im)) {
            g_resize_group_skipped++;
            continue;
        }
        bool seen = false;
        for (int j = 0; j < g_resize_group_n; j++)
            if (g_resize_group_slots[j] == slot) { seen = true; break; }
        if (seen) {
            g_resize_group_uses++;
            continue;
        }
        if (g_resize_group_n < RESIZE_GROUP_MAX) {
            g_resize_group_slots[g_resize_group_n++] = slot;
            g_resize_group_uses++;
        }
    }

    if (g_resize_group_n == 0) {
        stage_set_toast(g_resize_group_skipped > 0
                            ? "Selected sprites are read-only runtime art"
                            : "Select one or more sprites to resize");
        return;
    }
    if (g_resize_group_uses <= 1) {
        /* A single placement is clearer in the per-image dialog. Multiple
           placements of one tiled image stay in group mode so the layout can
           be scaled as a connected unit. */
        open_sprite_resize(g_resize_group_slots[0], true);
        return;
    }

    g_resize_group = true;
    g_resize_img = g_resize_group_slots[0];
    g_resize_pct = 100.0f;
    g_resize_anchor = 2;
    g_resize_selected_only = true;
    g_resize_connect = true;
    g_resize_open = true;
    g_resize_request = true;
}

static Uint8 *resize_indexed_pixels(const Img *im, int nw, int nh)
{
    if (!im || !im->pix || im->w <= 0 || im->h <= 0 || nw <= 0 || nh <= 0)
        return NULL;
    Uint8 *out = (Uint8 *)malloc((size_t)nw * (size_t)nh);
    if (!out) return NULL;
    for (int y = 0; y < nh; y++) {
        int sy = (int)(((long long)y * im->h) / nh);
        if (sy < 0) sy = 0;
        if (sy >= im->h) sy = im->h - 1;
        for (int x = 0; x < nw; x++) {
            int sx = (int)(((long long)x * im->w) / nw);
            if (sx < 0) sx = 0;
            if (sx >= im->w) sx = im->w - 1;
            out[y * nw + x] = im->pix[sy * im->w + sx];
        }
    }
    return out;
}

static void resize_anchor_delta(int anchor, int old_w, int old_h, int new_w, int new_h,
                                int *dx, int *dy)
{
    int dw = old_w - new_w;
    int dh = old_h - new_h;
    *dx = 0;
    *dy = 0;
    switch (anchor) {
        case 1: *dx = dw / 2; *dy = dh / 2; break;
        case 2: *dx = dw / 2; *dy = dh; break;
        case 3: *dx = dw;     *dy = dh; break;
        default: break;
    }
}

static int selected_uses_image(int ii)
{
    int count = 0;
    for (int i = 0; i < g_no; i++)
        if (g_sel_flags[i] && g_obj[i].ii == ii)
            count++;
    if (count == 0 && g_hl_obj >= 0 && g_hl_obj < g_no && g_obj[g_hl_obj].ii == ii)
        count = 1;
    return count;
}

/* Resize the image at g_img[slot] to nw x nh. Caller owns the undo snapshot and
   the g_dirty / g_need_rebuild bookkeeping. Returns the number of placements
   re-pointed/shifted, or -1 on a hard failure (out of memory / image bank
   full). A no-op size returns 0. When shift_positions is false the per-anchor
   placement offset is skipped so the caller can reposition placements itself
   (used by connected group scaling). */
static int resize_image_core(int slot, int nw, int nh, int anchor,
                             bool duplicate_selected, bool shift_positions)
{
    if (slot < 0 || slot >= g_ni) return -1;
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    Img *src = &g_img[slot];
    if (!src->pix) return -1;
    int old_idx = src->idx;
    int old_w = src->w;
    int old_h = src->h;
    if (old_w == nw && old_h == nh) return 0;

    Uint8 *new_pix = resize_indexed_pixels(src, nw, nh);
    if (!new_pix) return -1;

    int dx = 0, dy = 0;
    resize_anchor_delta(anchor, old_w, old_h, nw, nh, &dx, &dy);
    bool dup = duplicate_selected && selected_uses_image(old_idx) > 0;

    if (dup) {
        if (!editor_project_reserve_images(g_ni + 1)) {
            free(new_pix);
            return -1;
        }
        int new_idx = next_free_image_index(old_idx + 1);
        if (new_idx < 0) {
            free(new_pix);
            return -1;
        }
        src = &g_img[slot];
        Img *dst = editor_project_append_image_slot();
        if (!dst) {
            free(new_pix);
            return -1;
        }
        *dst = *src;
        dst->idx = new_idx;
        dst->w = nw;
        dst->h = nh;
        dst->pix = new_pix;
        if (dst->label[0]) {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%.54s resize", dst->label);
            snprintf(dst->label, sizeof dst->label, "%s", tmp);
        }

        int changed = 0;
        for (int i = 0; i < g_no; i++) {
            bool target = (g_sel_flags[i] && g_obj[i].ii == old_idx);
            if (!target && changed == 0 && selected_count() == 0 && i == g_hl_obj && g_obj[i].ii == old_idx)
                target = true;
            if (!target) continue;
            g_obj[i].ii = new_idx;
            if (dst->pal_idx >= 0) g_obj[i].fl = dst->pal_idx;
            if (shift_positions) {
                g_obj[i].depth += dx;
                g_obj[i].sy += dy;
            }
            changed++;
        }
        return changed;
    }

    free(src->pix);
    src->pix = new_pix;
    src->w = nw;
    src->h = nh;
    int changed = 0;
    for (int i = 0; i < g_no; i++) {
        if (g_obj[i].ii != old_idx) continue;
        if (shift_positions) {
            g_obj[i].depth += dx;
            g_obj[i].sy += dy;
        }
        changed++;
    }
    return changed;
}

static void apply_sprite_resize(void)
{
    if (g_resize_img < 0 || g_resize_img >= g_ni) return;
    if (g_resize_w < 1) g_resize_w = 1;
    if (g_resize_h < 1) g_resize_h = 1;
    Img *src = &g_img[g_resize_img];
    if (!src->pix) return;
    if (src->w == g_resize_w && src->h == g_resize_h) {
        stage_set_toast("Sprite is already that size");
        return;
    }
    int old_idx = src->idx;

    undo_save_ex("Resize Sprite");
    int changed = resize_image_core(g_resize_img, g_resize_w, g_resize_h,
                                    g_resize_anchor, g_resize_selected_only, true);
    if (changed < 0) {
        stage_set_toast("Resize failed: out of memory or image bank full");
        return;
    }

    char msg[96];
    snprintf(msg, sizeof msg, "Resized image 0x%X to %dx%d", old_idx, g_resize_w, g_resize_h);
    stage_set_toast(msg);
    g_dirty = 1;
    g_need_rebuild = 1;
    g_resize_open = false;
}

/* Iterate the placements that the group operates on: the current selection,
   or the highlighted object when nothing is multi-selected. */
static bool group_placement_selected(int i, bool any_sel)
{
    if (i < 0 || i >= g_no) return false;
    return any_sel ? (g_sel_flags[i] != 0) : (i == g_hl_obj);
}

static int round_to_int(double v)
{
    return (int)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

static void apply_group_resize(void)
{
    if (g_resize_group_n <= 0) return;

    int will_change = 0;
    for (int k = 0; k < g_resize_group_n; k++) {
        int slot = g_resize_group_slots[k];
        if (slot < 0 || slot >= g_ni) continue;
        Img *im = &g_img[slot];
        if (!im->pix) continue;
        if (scale_dim(im->w, g_resize_pct) != im->w ||
            scale_dim(im->h, g_resize_pct) != im->h)
            will_change++;
    }
    if (will_change == 0) {
        stage_set_toast("Resize leaves every sprite at its current size");
        return;
    }

    /* For connected scaling, find the pivot from the selection's bounding box
       (using current positions and sizes) before anything is resized. Scaling
       every placement's top-left about this pivot by the same factor keeps a
       tiled layout seamless: a neighbour's new left edge lands on the previous
       sprite's new right edge. */
    bool any_sel = selected_count() > 0;
    double k = (double)g_resize_pct / 100.0;
    double pivot_x = 0.0, pivot_y = 0.0;
    bool have_pivot = false;
    if (g_resize_connect) {
        int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        for (int i = 0; i < g_no; i++) {
            if (!group_placement_selected(i, any_sel)) continue;
            Img *im = img_find(g_obj[i].ii);
            if (!im) continue;
            int x0 = g_obj[i].depth, y0 = g_obj[i].sy;
            int x1 = x0 + im->w, y1 = y0 + im->h;
            if (!have_pivot) {
                min_x = x0; min_y = y0; max_x = x1; max_y = y1;
                have_pivot = true;
            } else {
                if (x0 < min_x) min_x = x0;
                if (y0 < min_y) min_y = y0;
                if (x1 > max_x) max_x = x1;
                if (y1 > max_y) max_y = y1;
            }
        }
        if (have_pivot) {
            double cx = (min_x + max_x) / 2.0;
            double cy = (min_y + max_y) / 2.0;
            switch (g_resize_anchor) {
                case 0: pivot_x = min_x; pivot_y = min_y; break; /* top-left */
                case 1: pivot_x = cx;    pivot_y = cy;    break; /* center */
                case 3: pivot_x = max_x; pivot_y = max_y; break; /* bottom-right */
                default: pivot_x = cx;   pivot_y = max_y; break; /* bottom-center */
            }
        }
    }

    undo_save_ex("Resize Sprite Group");

    /* In connected mode we reposition placements ourselves about the pivot, so
       the per-anchor in-place shift is suppressed. */
    bool shift_positions = !(g_resize_connect && have_pivot);

    int imgs = 0, placements = 0, failed = 0;
    for (int k2 = 0; k2 < g_resize_group_n; k2++) {
        int slot = g_resize_group_slots[k2];
        if (slot < 0 || slot >= g_ni) continue;
        Img *im = &g_img[slot];
        if (!im->pix) continue;
        int nw = scale_dim(im->w, g_resize_pct);
        int nh = scale_dim(im->h, g_resize_pct);
        if (nw == im->w && nh == im->h) continue;
        int ch = resize_image_core(slot, nw, nh, g_resize_anchor,
                                   g_resize_selected_only, shift_positions);
        if (ch < 0) { failed++; continue; }
        imgs++;
        placements += ch;
    }

    if (g_resize_connect && have_pivot) {
        for (int i = 0; i < g_no; i++) {
            if (!group_placement_selected(i, any_sel)) continue;
            g_obj[i].depth = round_to_int(pivot_x + (g_obj[i].depth - pivot_x) * k);
            g_obj[i].sy    = round_to_int(pivot_y + (g_obj[i].sy    - pivot_y) * k);
        }
    }

    char msg[128];
    if (failed > 0)
        snprintf(msg, sizeof msg, "Resized %d sprite%s to %d%% (%d failed)",
                 imgs, imgs == 1 ? "" : "s", (int)(g_resize_pct + 0.5f), failed);
    else
        snprintf(msg, sizeof msg, "Resized %d sprite%s to %d%% (%d placement%s)",
                 imgs, imgs == 1 ? "" : "s", (int)(g_resize_pct + 0.5f),
                 placements, placements == 1 ? "" : "s");
    stage_set_toast(msg);
    g_dirty = 1;
    g_need_rebuild = 1;
    g_resize_open = false;
}

/* A before/after thumbnail of one image at the chosen percentage. The current
   texture is drawn at a baseline scale times pct, so shrinking and growing both
   read at a glance; the numeric dimensions are shown beneath. */
static void draw_image_preview_cell(int slot, float pct, float baseline_px)
{
    if (slot < 0 || slot >= g_ni) return;
    Img *im = &g_img[slot];
    if (!im->pix || im->w <= 0 || im->h <= 0) return;

    int nw = scale_dim(im->w, pct);
    int nh = scale_dim(im->h, pct);

    int max_dim = im->w > im->h ? im->w : im->h;
    float ref = max_dim > 0 ? baseline_px / (float)max_dim : 1.0f;
    float disp_w = im->w * ref * (pct / 100.0f);
    float disp_h = im->h * ref * (pct / 100.0f);
    float cap = baseline_px * 2.0f;
    if (disp_w > cap) { disp_h *= cap / disp_w; disp_w = cap; }
    if (disp_h > cap) { disp_w *= cap / disp_h; disp_h = cap; }
    if (disp_w < 2.0f) disp_w = 2.0f;
    if (disp_h < 2.0f) disp_h = 2.0f;

    ImGui::BeginGroup();
    SDL_Texture *tex = editor_texture_at(slot);
    if (tex)
        draw_editor_texture_transparent(tex, disp_w, disp_h);
    else
        ImGui::Dummy(ImVec2(disp_w, disp_h));
    if (im->label[0])
        ImGui::TextDisabled("%.20s", im->label);
    ImGui::TextDisabled("%dx%d -> %dx%d", im->w, im->h, nw, nh);
    ImGui::EndGroup();
}

static void draw_anchor_and_duplicate_controls(int sel_use_count, int use_count, bool group)
{
    const char *anchors[] = {"Top-left", "Center", "Bottom-center", "Bottom-right"};
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Placement anchor", anchors[g_resize_anchor])) {
        for (int i = 0; i < 4; i++) {
            if (ImGui::Selectable(anchors[i], g_resize_anchor == i))
                g_resize_anchor = i;
        }
        ImGui::EndCombo();
    }

    bool can_selected_only = sel_use_count > 0;
    if (!can_selected_only)
        ImGui::BeginDisabled();
    ImGui::Checkbox("Duplicate for selected placement(s)", &g_resize_selected_only);
    if (!can_selected_only)
        ImGui::EndDisabled();
    if (g_resize_selected_only && can_selected_only)
        ImGui::TextDisabled("Only %d selected use%s will switch to the resized copy.",
                            sel_use_count, sel_use_count == 1 ? "" : "s");
    else
        ImGui::TextDisabled("Resize applies to all %d placement%s using %s image%s.",
                            use_count, use_count == 1 ? "" : "s",
                            group ? "these" : "this", group ? "s" : "");
}

static void draw_single_resize_body(Img *im)
{
    int use_count = image_use_count(im->idx);
    int sel_use_count = selected_uses_image(im->idx);
    ImGui::Text(g_simple_mode ? "Image %d" : "Image 0x%04X", im->idx);
    ImGui::SameLine();
    ImGui::TextDisabled("%dx%d, %d use%s", im->w, im->h, use_count, use_count == 1 ? "" : "s");
    ImGui::Separator();

    ImGui::SetNextItemWidth(110);
    if (ImGui::InputInt("Width", &g_resize_w)) {
        if (g_resize_w < 1) g_resize_w = 1;
        if (g_resize_keep_aspect && g_resize_orig_w > 0)
            g_resize_h = (int)(((long long)g_resize_w * g_resize_orig_h + g_resize_orig_w / 2) / g_resize_orig_w);
        if (g_resize_h < 1) g_resize_h = 1;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputInt("Height", &g_resize_h)) {
        if (g_resize_h < 1) g_resize_h = 1;
        if (g_resize_keep_aspect && g_resize_orig_h > 0)
            g_resize_w = (int)(((long long)g_resize_h * g_resize_orig_w + g_resize_orig_h / 2) / g_resize_orig_h);
        if (g_resize_w < 1) g_resize_w = 1;
    }
    if (g_resize_w > 4096) g_resize_w = 4096;
    if (g_resize_h > 4096) g_resize_h = 4096;
    ImGui::Checkbox("Keep aspect", &g_resize_keep_aspect);

    /* Percentage slider, kept in sync with the width/height fields. */
    if (g_resize_orig_w > 0)
        g_resize_pct = 100.0f * (float)g_resize_w / (float)g_resize_orig_w;
    ImGui::SetNextItemWidth(232);
    if (ImGui::SliderFloat("Percent", &g_resize_pct, 5.0f, 400.0f, "%.0f%%")) {
        g_resize_w = scale_dim(g_resize_orig_w, g_resize_pct);
        g_resize_h = scale_dim(g_resize_orig_h, g_resize_pct);
    }

    if (ImGui::SmallButton("25%"))  { g_resize_w = scale_dim(g_resize_orig_w, 25.0f);  g_resize_h = scale_dim(g_resize_orig_h, 25.0f); }
    ImGui::SameLine();
    if (ImGui::SmallButton("50%"))  { g_resize_w = scale_dim(g_resize_orig_w, 50.0f);  g_resize_h = scale_dim(g_resize_orig_h, 50.0f); }
    ImGui::SameLine();
    if (ImGui::SmallButton("100%")) { g_resize_w = g_resize_orig_w; g_resize_h = g_resize_orig_h; }
    ImGui::SameLine();
    if (ImGui::SmallButton("200%")) { g_resize_w = scale_dim(g_resize_orig_w, 200.0f); g_resize_h = scale_dim(g_resize_orig_h, 200.0f); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit 400w") && g_resize_orig_w > 0) {
        g_resize_w = 400;
        g_resize_h = (int)(((long long)400 * g_resize_orig_h + g_resize_orig_w / 2) / g_resize_orig_w);
    }

    draw_anchor_and_duplicate_controls(sel_use_count, use_count, false);

    size_t old_px = (size_t)im->w * (size_t)im->h;
    size_t new_px = (size_t)g_resize_w * (size_t)g_resize_h;
    ImGui::TextDisabled("Indexed nearest resize. Palette indexes stay unchanged.");
    ImGui::TextDisabled("Payload: %.1f KB -> %.1f KB", old_px / 1024.0, new_px / 1024.0);

    ImGui::Separator();
    ImGui::TextDisabled("Preview");
    float pct = g_resize_orig_w > 0 ? 100.0f * (float)g_resize_w / (float)g_resize_orig_w : 100.0f;
    draw_image_preview_cell(g_resize_img, pct, 64.0f);
}

static void draw_group_resize_body(void)
{
    ImGui::Text("Resize %d sprite%s", g_resize_group_n, g_resize_group_n == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextDisabled("across %d placement%s", g_resize_group_uses,
                        g_resize_group_uses == 1 ? "" : "s");
    if (g_resize_group_skipped > 0)
        ImGui::TextDisabled("%d read-only runtime sprite%s skipped",
                            g_resize_group_skipped, g_resize_group_skipped == 1 ? "" : "s");
    ImGui::Separator();

    ImGui::SetNextItemWidth(232);
    ImGui::SliderFloat("Percent", &g_resize_pct, 5.0f, 400.0f, "%.0f%%");
    if (ImGui::SmallButton("25%"))  g_resize_pct = 25.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("50%"))  g_resize_pct = 50.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("100%")) g_resize_pct = 100.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("150%")) g_resize_pct = 150.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("200%")) g_resize_pct = 200.0f;

    ImGui::Checkbox("Keep sprites connected (scale layout)", &g_resize_connect);
    if (g_resize_connect)
        ImGui::TextDisabled("Spacing between selected sprites scales too, so a tiled\n"
                            "group stays seamless. Anchor is the group's fixed corner.");
    else
        ImGui::TextDisabled("Each sprite resizes in place about its own anchor.");

    /* How many of the selection's placements use these images, so the
       "duplicate for selected" wording is accurate for the whole group. */
    int sel_use_total = 0, use_total = 0;
    for (int k = 0; k < g_resize_group_n; k++) {
        int slot = g_resize_group_slots[k];
        if (slot < 0 || slot >= g_ni) continue;
        sel_use_total += selected_uses_image(g_img[slot].idx);
        use_total += image_use_count(g_img[slot].idx);
    }
    draw_anchor_and_duplicate_controls(sel_use_total, use_total, true);
    ImGui::TextDisabled("Indexed nearest resize. Palette indexes stay unchanged.");

    ImGui::Separator();
    ImGui::TextDisabled("Preview");
    ImGui::BeginChild("group_resize_preview", ImVec2(0, 200), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    int shown = g_resize_group_n < 12 ? g_resize_group_n : 12;
    float avail_w = ImGui::GetContentRegionAvail().x;
    float x_used = 0.0f;
    for (int k = 0; k < shown; k++) {
        if (k > 0) {
            x_used += 96.0f;
            if (x_used + 96.0f <= avail_w) ImGui::SameLine();
            else x_used = 0.0f;
        }
        draw_image_preview_cell(g_resize_group_slots[k], g_resize_pct, 56.0f);
    }
    if (g_resize_group_n > shown)
        ImGui::TextDisabled("+%d more sprite%s", g_resize_group_n - shown,
                            (g_resize_group_n - shown) == 1 ? "" : "s");
    ImGui::EndChild();
}

void draw_sprite_resize_dialog(void)
{
    if (g_resize_request) {
        ImGui::OpenPopup("Resize Sprite");
        g_resize_request = false;
    }
    if (!g_resize_open) return;

    bool modal_open = true;
    if (ImGui::BeginPopupModal("Resize Sprite", &modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g_resize_group) {
            if (g_resize_group_n <= 0) {
                g_resize_open = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }
            draw_group_resize_body();

            ImGui::Separator();
            bool valid = g_resize_pct >= 5.0f;
            if (!valid) ImGui::BeginDisabled();
            if (ImGui::Button("Apply", ImVec2(120, 0))) {
                apply_group_resize();
                ImGui::CloseCurrentPopup();
            }
            if (!valid) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                g_resize_open = false;
                ImGui::CloseCurrentPopup();
            }
            if (!modal_open)
                g_resize_open = false;
            ImGui::EndPopup();
            return;
        }

        Img *im = (g_resize_img >= 0 && g_resize_img < g_ni) ? &g_img[g_resize_img] : NULL;
        if (!im || !im->pix) {
            ImGui::TextUnformatted("No image selected.");
            if (ImGui::Button("Close")) {
                g_resize_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        draw_single_resize_body(im);

        ImGui::Separator();
        bool valid = g_resize_w > 0 && g_resize_h > 0 && (g_resize_w != im->w || g_resize_h != im->h);
        if (!valid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            apply_sprite_resize();
            ImGui::CloseCurrentPopup();
        }
        if (!valid)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            g_resize_open = false;
            ImGui::CloseCurrentPopup();
        }
        if (!modal_open)
            g_resize_open = false;
        ImGui::EndPopup();
    }
}
