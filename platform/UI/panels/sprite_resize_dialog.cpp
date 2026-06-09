#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static bool  g_resize_open     = false;
static bool  g_resize_request  = false;
static int   g_resize_img      = -1;
static int   g_resize_orig_w   = 0;
static int   g_resize_orig_h   = 0;
static int   g_resize_w        = 1;
static int   g_resize_h        = 1;
static int   g_resize_anchor   = 2;
static bool  g_resize_keep_aspect = true;
static bool  g_resize_selected_only = true;

void open_sprite_resize(int img_idx, bool selected_only_default)
{
    if (img_idx < 0 || img_idx >= g_ni) return;
    Img *im = &g_img[img_idx];
    if (!im->pix || im->w <= 0 || im->h <= 0) return;
    g_resize_img = img_idx;
    g_resize_orig_w = im->w;
    g_resize_orig_h = im->h;
    g_resize_w = im->w;
    g_resize_h = im->h;
    g_resize_keep_aspect = true;
    g_resize_anchor = 2;
    g_resize_selected_only = selected_only_default;
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

static void apply_sprite_resize(void)
{
    if (g_resize_img < 0 || g_resize_img >= g_ni) return;
    if (g_resize_w < 1) g_resize_w = 1;
    if (g_resize_h < 1) g_resize_h = 1;
    Img *src = &g_img[g_resize_img];
    if (!src->pix) return;
    int old_idx = src->idx;
    int old_w = src->w;
    int old_h = src->h;
    if (old_w == g_resize_w && old_h == g_resize_h) {
        stage_set_toast("Sprite is already that size");
        return;
    }
    Uint8 *new_pix = resize_indexed_pixels(src, g_resize_w, g_resize_h);
    if (!new_pix) {
        stage_set_toast("Resize failed: out of memory");
        return;
    }

    int dx = 0, dy = 0;
    resize_anchor_delta(g_resize_anchor, old_w, old_h, g_resize_w, g_resize_h, &dx, &dy);
    bool duplicate_selected = g_resize_selected_only && selected_uses_image(old_idx) > 0;

    undo_save_ex("Resize Sprite");

    if (duplicate_selected) {
        if (!editor_project_reserve_images(g_ni + 1)) {
            free(new_pix);
            stage_set_toast("Resize failed: image bank is full");
            return;
        }
        int new_idx = next_free_image_index(old_idx + 1);
        if (new_idx < 0) {
            free(new_pix);
            stage_set_toast("Resize failed: no free image index");
            return;
        }
        src = &g_img[g_resize_img];
        Img *dst = editor_project_append_image_slot();
        if (!dst) {
            free(new_pix);
            stage_set_toast("Resize failed: image bank is full");
            return;
        }
        *dst = *src;
        dst->idx = new_idx;
        dst->w = g_resize_w;
        dst->h = g_resize_h;
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
            g_obj[i].depth += dx;
            g_obj[i].sy += dy;
            changed++;
        }
        char msg[96];
        snprintf(msg, sizeof msg, "Resized copy 0x%X -> 0x%X (%dx%d)", old_idx, new_idx, g_resize_w, g_resize_h);
        stage_set_toast(msg);
    } else {
        free(src->pix);
        src->pix = new_pix;
        src->w = g_resize_w;
        src->h = g_resize_h;
        for (int i = 0; i < g_no; i++) {
            if (g_obj[i].ii != old_idx) continue;
            g_obj[i].depth += dx;
            g_obj[i].sy += dy;
        }
        char msg[96];
        snprintf(msg, sizeof msg, "Resized image 0x%X to %dx%d", old_idx, g_resize_w, g_resize_h);
        stage_set_toast(msg);
    }

    g_dirty = 1;
    g_need_rebuild = 1;
    g_resize_open = false;
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

        int use_count = image_use_count(im->idx);
        int sel_use_count = selected_uses_image(im->idx);
        ImGui::Text(g_simple_mode ? "Image %d" : "Image 0x%04X", im->idx);
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d, %d use%s", im->w, im->h, use_count, use_count == 1 ? "" : "s");
        ImGui::Separator();

        int prev_w = g_resize_w;
        int prev_h = g_resize_h;
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

        if (ImGui::SmallButton("50%")) {
            g_resize_w = (g_resize_orig_w + 1) / 2;
            g_resize_h = (g_resize_orig_h + 1) / 2;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("100%")) {
            g_resize_w = g_resize_orig_w;
            g_resize_h = g_resize_orig_h;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("200%")) {
            g_resize_w = g_resize_orig_w * 2;
            g_resize_h = g_resize_orig_h * 2;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Fit 400w") && g_resize_orig_w > 0) {
            g_resize_w = 400;
            g_resize_h = (int)(((long long)400 * g_resize_orig_h + g_resize_orig_w / 2) / g_resize_orig_w);
        }

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
            ImGui::TextDisabled("Resize applies to all %d placement%s using this image.",
                                use_count, use_count == 1 ? "" : "s");

        size_t old_px = (size_t)im->w * (size_t)im->h;
        size_t new_px = (size_t)g_resize_w * (size_t)g_resize_h;
        ImGui::TextDisabled("Indexed nearest resize. Palette indexes stay unchanged.");
        ImGui::TextDisabled("Payload: %.1f KB -> %.1f KB", old_px / 1024.0, new_px / 1024.0);

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
        (void)prev_w;
        (void)prev_h;
        ImGui::EndPopup();
    }
}
