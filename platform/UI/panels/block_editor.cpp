#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

bool g_block_edit_open = false;
int  g_block_edit_img  = -1;
int  g_block_edit_zoom = 8;
int  g_block_edit_col  = 0;

static int   g_block_edit_bg   = 0; /* 0=checker, 1=hot pink, 2=neon green, 3=black, 4=white */
static bool  g_block_edit_grid = true;
static bool  g_block_edit_auto_fit = true;
static int   g_block_edit_last_img = -1;
static int   g_block_match_ref_obj = -1;
static bool  g_block_match_used_colors_only = true;
static bool  g_block_match_all_uses = true;
static float g_block_match_shade_weight = 0.55f;
static char  g_block_match_status[160] = "";
static bool  g_block_tone_used_colors_only = true;
static float g_block_tone_brightness = 0.0f;
static float g_block_tone_contrast = 0.0f;

struct BlockPixelUndoCapture {
    bool active = false;
    int img_i = -1;
    int width = 0;
    int height = 0;
    std::vector<Uint8> before_pixels;
};

static BlockPixelUndoCapture g_block_pixel_undo;

struct BlockPaletteUndoCapture {
    bool active = false;
    int pal_i = -1;
    int color_i = -1;
    int count = 0;
    char name[64] = "";
    Uint32 colors[256] = {};
    Uint16 rgb555[256] = {};
    int rgb555_valid = 0;
    int rgb555_count = 0;
};

static BlockPaletteUndoCapture g_block_palette_color_undo;
static BlockPaletteUndoCapture g_block_palette_tone_undo;

/* Brush-stroke state so a drag paints a continuous line instead of only the
   per-frame hovered cell. Without this, fast drags skip the pixels between
   sampled mouse positions, leaving stray gaps (transparent where the art was
   already index 0). */
static bool g_block_paint_active = false;
static int  g_block_paint_last_x = -1;
static int  g_block_paint_last_y = -1;

static void block_paint_pixel(Img *im, int x, int y, Uint8 col)
{
    if (!im || !im->pix || x < 0 || y < 0 || x >= im->w || y >= im->h)
        return;
    im->pix[(size_t)y * (size_t)im->w + (size_t)x] = col;
}

/* Paint every cell on the line from (x0,y0) to (x1,y1) so dragged strokes are
   continuous regardless of mouse speed. */
static void block_paint_line(Img *im, int x0, int y0, int x1, int y1, Uint8 col)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        block_paint_pixel(im, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void block_pixel_undo_begin(int img_i, const Img *im)
{
    if (!im || !im->pix || im->w <= 0 || im->h <= 0 || im->w > 0x3fffffff / im->h)
        return;
    g_block_pixel_undo.active = true;
    g_block_pixel_undo.img_i = img_i;
    g_block_pixel_undo.width = im->w;
    g_block_pixel_undo.height = im->h;
    int pixel_count = im->w * im->h;
    g_block_pixel_undo.before_pixels.assign(im->pix, im->pix + pixel_count);
}

static void block_pixel_undo_commit(void)
{
    if (!g_block_pixel_undo.active)
        return;
    undo_save_image_pixels_delta(g_block_pixel_undo.img_i,
                                 g_block_pixel_undo.width,
                                 g_block_pixel_undo.height,
                                 g_block_pixel_undo.before_pixels.data(),
                                 "Edit Pixels");
    g_block_pixel_undo = BlockPixelUndoCapture{};
}

static void block_palette_undo_capture(BlockPaletteUndoCapture *cap, int pal_i, int color_i)
{
    if (!cap) return;
    *cap = BlockPaletteUndoCapture{};
    if (pal_i < 0 || pal_i >= g_n_pals) return;
    cap->active = true;
    cap->pal_i = pal_i;
    cap->color_i = color_i;
    cap->count = g_pal_count[pal_i];
    snprintf(cap->name, sizeof cap->name, "%s", g_pal_name[pal_i]);
    memcpy(cap->colors, g_pals[pal_i], sizeof cap->colors);
    cap->rgb555_valid = editor_project_get_palette_rgb555_cache(pal_i, cap->rgb555, 256);
    cap->rgb555_count = cap->rgb555_valid ? g_pal_count[pal_i] : 0;
}

static int block_palette_undo_commit(BlockPaletteUndoCapture *cap, const char *label)
{
    int saved = 0;
    if (!cap || !cap->active) return 0;
    saved = undo_save_palette_slot_delta(cap->pal_i, cap->colors, cap->count,
                                         cap->name, cap->rgb555,
                                         cap->rgb555_valid, cap->rgb555_count,
                                         label);
    *cap = BlockPaletteUndoCapture{};
    return saved;
}

static void block_palette_mark_changed(void)
{
    g_dirty = 1;
    g_need_rebuild = 1;
    g_mk2_palette_sync_dirty = true;
}

static void block_editor_draw_checkerboard_background(ImVec2 min, ImVec2 max, float cell)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 c0 = IM_COL32(48, 48, 52, 255);
    const ImU32 c1 = IM_COL32(92, 92, 96, 255);
    int row = 0;
    for (float y = min.y; y < max.y; y += cell, row++) {
        int col = 0;
        float y2 = y + cell;
        if (y2 > max.y) y2 = max.y;
        for (float x = min.x; x < max.x; x += cell, col++) {
            float x2 = x + cell;
            if (x2 > max.x) x2 = max.x;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x2, y2), ((row + col) & 1) ? c1 : c0);
        }
    }
}

static void block_editor_draw_solid_backdrop_image(SDL_Texture *tex, ImVec2 size, bool white)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 q(p.x + size.x, p.y + size.y);
    dl->AddRectFilled(p, q, white ? IM_COL32(255, 255, 255, 255)
                                  : IM_COL32(0, 0, 0, 255));
    ImGui::Image(tex, size);
}

static void block_editor_clamp_state(int palette_count)
{
    if (g_block_edit_zoom < 1) g_block_edit_zoom = 1;
    if (g_block_edit_zoom > 64) g_block_edit_zoom = 64;
    if (palette_count < 1) palette_count = 1;
    if (g_block_edit_col < 0) g_block_edit_col = 0;
    if (g_block_edit_col >= palette_count) g_block_edit_col = palette_count - 1;
    if (g_block_edit_bg < 0 || g_block_edit_bg > 4) g_block_edit_bg = 0;
}

static int block_editor_fit_zoom(const Img *im, float canvas_w, float work_h)
{
    if (!im || im->w <= 0 || im->h <= 0)
        return 8;
    float usable_w = canvas_w - 28.0f;
    float usable_h = work_h - 42.0f;
    if (usable_w < 96.0f) usable_w = 96.0f;
    if (usable_h < 96.0f) usable_h = 96.0f;
    int fit_x = (int)(usable_w / (float)im->w);
    int fit_y = (int)(usable_h / (float)im->h);
    int fit = fit_x < fit_y ? fit_x : fit_y;
    if (fit < 1) fit = 1;
    if (fit > 64) fit = 64;
    return fit;
}

static int block_editor_clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static Uint32 block_editor_adjust_color(Uint32 c, float brightness, float contrast)
{
    float contrast255 = contrast * 2.55f;
    if (contrast255 < -254.0f) contrast255 = -254.0f;
    if (contrast255 > 254.0f) contrast255 = 254.0f;
    float factor = (259.0f * (contrast255 + 255.0f)) /
                   (255.0f * (259.0f - contrast255));
    float offset = brightness * 2.55f;
    int r = (int)(factor * (((c >> 16) & 0xFF) - 128) + 128 + offset + 0.5f);
    int g = (int)(factor * (((c >> 8) & 0xFF) - 128) + 128 + offset + 0.5f);
    int b = (int)(factor * ((c & 0xFF) - 128) + 128 + offset + 0.5f);
    return (c & 0xFF000000u) |
           ((Uint32)block_editor_clamp_u8(r) << 16) |
           ((Uint32)block_editor_clamp_u8(g) << 8) |
           (Uint32)block_editor_clamp_u8(b);
}

static int block_editor_collect_tone_indexes(const Img *im, int palette_count,
                                             bool used_only, bool *used)
{
    int count = 0;
    if (!used) return 0;
    memset(used, 0, sizeof(bool) * 256);
    if (palette_count < 0) palette_count = 0;
    if (palette_count > 256) palette_count = 256;
    if (!used_only) {
        for (int i = 1; i < palette_count; i++) {
            used[i] = true;
            count++;
        }
        return count;
    }
    if (!im || !im->pix || im->w <= 0 || im->h <= 0)
        return 0;
    size_t n = (size_t)im->w * (size_t)im->h;
    for (size_t i = 0; i < n; i++) {
        int v = im->pix[i];
        if (v <= 0 || v >= palette_count || used[v]) continue;
        used[v] = true;
        count++;
    }
    return count;
}

static void block_editor_apply_tone_from_capture(const Img *im, int palette_count)
{
    if (!g_block_palette_tone_undo.active)
        return;
    bool used[256];
    Uint32 colors[256];
    block_editor_collect_tone_indexes(im, palette_count,
                                      g_block_tone_used_colors_only, used);
    memcpy(colors, g_block_palette_tone_undo.colors, sizeof colors);
    for (int i = 1; i < palette_count && i < 256; i++) {
        if (!used[i]) continue;
        colors[i] = block_editor_adjust_color(g_block_palette_tone_undo.colors[i],
                                              g_block_tone_brightness,
                                              g_block_tone_contrast);
    }
    if (editor_project_set_palette_slot(g_block_palette_tone_undo.pal_i, NULL,
                                        g_block_palette_tone_undo.count, colors))
        block_palette_mark_changed();
}

static void block_editor_finish_tone_edit(void)
{
    if (block_palette_undo_commit(&g_block_palette_tone_undo, "Adjust Palette Tone") > 0)
        stage_set_toast("Adjusted palette tone");
    g_block_tone_brightness = 0.0f;
    g_block_tone_contrast = 0.0f;
}

static ImU32 block_editor_bg_color(void)
{
    switch (g_block_edit_bg) {
        case 1: return IM_COL32(255, 0, 170, 255);
        case 2: return IM_COL32(0, 255, 80, 255);
        case 3: return IM_COL32(0, 0, 0, 255);
        case 4: return IM_COL32(255, 255, 255, 255);
        default: return IM_COL32(0, 0, 0, 0);
    }
}

static void block_editor_draw_backdrop(ImDrawList *dl, ImVec2 origin, float w, float h)
{
    ImVec2 bottom_right(origin.x + w, origin.y + h);
    if (g_block_edit_bg == 0)
        block_editor_draw_checkerboard_background(origin, bottom_right, 8.0f);
    else
        dl->AddRectFilled(origin, bottom_right, block_editor_bg_color());
}

static void block_editor_bg_option(const char *label, int mode, ImU32 color)
{
    ImGui::PushID(label);
    if (mode > 0) {
        if (ImGui::ColorButton("##bg", ImGui::ColorConvertU32ToFloat4(color),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(22, 18)))
            g_block_edit_bg = mode;
        ImGui::SameLine();
    }
    if (ImGui::RadioButton(label, g_block_edit_bg == mode))
        g_block_edit_bg = mode;
    ImGui::PopID();
}

void draw_block_editor(void)
{
    if (!g_block_edit_open || g_block_edit_img < 0 || g_block_edit_img >= g_ni)
        return;

    Img *im = &g_img[g_block_edit_img];
    if (!im->pix) return;

    const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                       ? g_pals[im->pal_idx] : NULL;
    int pc = pal ? g_pal_count[im->pal_idx] : 256;
    if (pc > 256) pc = 256;
    block_editor_clamp_state(pc);
    if (g_block_palette_color_undo.active &&
        (g_block_palette_color_undo.pal_i != im->pal_idx ||
         g_block_palette_color_undo.color_i != g_block_edit_col))
        block_palette_undo_commit(&g_block_palette_color_undo, "Edit Palette Color");
    if (g_block_palette_tone_undo.active &&
        g_block_palette_tone_undo.pal_i != im->pal_idx)
        block_editor_finish_tone_edit();

    set_left_panel_default(92.0f, 920.0f, 620.0f);
    if (!ImGui::Begin("Block Editor", &g_block_edit_open)) {
        ImGui::End();
        return;
    }

    char image_label[128];
    if (im->label[0])
        snprintf(image_label, sizeof image_label, "%s  0x%02X  %dx%d  pal %d",
                 im->label, im->idx, im->w, im->h, im->pal_idx);
    else
        snprintf(image_label, sizeof image_label, "Image 0x%02X  %dx%d  pal %d",
                 im->idx, im->w, im->h, im->pal_idx);
    ImGui::TextUnformatted(image_label);
    if (im->source[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", im->source);
    }

    float avail_w = ImGui::GetContentRegionAvail().x;
    float side_w = 270.0f;
    if (im->w <= 32 && im->h <= 32) side_w = 250.0f;
    else if (im->w >= 160 || im->h >= 160) side_w = 300.0f;
    if (avail_w < 760.0f) side_w = 240.0f;
    if (side_w > avail_w * 0.45f) side_w = avail_w * 0.45f;
    float canvas_w = avail_w - side_w - ImGui::GetStyle().ItemSpacing.x;
    if (canvas_w < 280.0f) canvas_w = avail_w - side_w - 4.0f;
    if (canvas_w < 220.0f) canvas_w = 220.0f;
    float work_h = ImGui::GetContentRegionAvail().y;
    if (work_h < 420.0f) work_h = 420.0f;
    int fit_zoom = block_editor_fit_zoom(im, canvas_w, work_h);
    if (g_block_edit_last_img != g_block_edit_img) {
        g_block_edit_last_img = g_block_edit_img;
        g_block_edit_auto_fit = true;
        g_block_edit_zoom = fit_zoom;
    } else if (g_block_edit_auto_fit) {
        g_block_edit_zoom = fit_zoom;
    }
    block_editor_clamp_state(pc);

    int hover_x = -1, hover_y = -1;

    ImGui::BeginChild("block_canvas", ImVec2(canvas_w, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    bool canvas_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (canvas_hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
        g_block_edit_zoom += (ImGui::GetIO().MouseWheel > 0.0f) ? 1 : -1;
        g_block_edit_auto_fit = false;
        block_editor_clamp_state(pc);
    }

    int zoom = g_block_edit_zoom;
    float pw = (float)im->w * (float)zoom;
    float ph = (float)im->h * (float)zoom;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    block_editor_draw_backdrop(dl, origin, pw, ph);

    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            Uint8 v = im->pix[y * im->w + x];
            if (v == 0) continue;
            ImU32 col;
            if (pal) {
                Uint32 c = pal[v];
                col = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
            } else {
                col = IM_COL32(v, v, v, 255);
            }
            dl->AddRectFilled(ImVec2(origin.x + x * zoom, origin.y + y * zoom),
                              ImVec2(origin.x + (x + 1) * zoom, origin.y + (y + 1) * zoom),
                              col);
        }
    }

    if (g_block_edit_grid && zoom >= 4) {
        ImU32 grid_col = g_block_edit_bg == 4 ? IM_COL32(0, 0, 0, 70) : IM_COL32(255, 255, 255, 55);
        ImU32 major_col = g_block_edit_bg == 4 ? IM_COL32(0, 0, 0, 120) : IM_COL32(255, 255, 255, 105);
        for (int x = 1; x < im->w; x++) {
            ImU32 col = (x % 8 == 0) ? major_col : grid_col;
            dl->AddLine(ImVec2(origin.x + x * zoom, origin.y),
                        ImVec2(origin.x + x * zoom, origin.y + ph), col);
        }
        for (int y = 1; y < im->h; y++) {
            ImU32 col = (y % 8 == 0) ? major_col : grid_col;
            dl->AddLine(ImVec2(origin.x, origin.y + y * zoom),
                        ImVec2(origin.x + pw, origin.y + y * zoom), col);
        }
    }
    dl->AddRect(origin, ImVec2(origin.x + pw, origin.y + ph),
                IM_COL32(255, 255, 255, 120));

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("pixcanvas", ImVec2(pw, ph));
    if (ImGui::IsItemHovered()) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int gx = (int)((mp.x - origin.x) / zoom);
        int gy = (int)((mp.y - origin.y) / zoom);
        if (gx >= 0 && gx < im->w && gy >= 0 && gy < im->h) {
            hover_x = gx;
            hover_y = gy;
            if (ImGui::IsMouseClicked(0)) {
                block_pixel_undo_begin(g_block_edit_img, im);
                block_paint_pixel(im, gx, gy, (Uint8)g_block_edit_col);
                g_block_paint_active = true;
                g_block_paint_last_x = gx;
                g_block_paint_last_y = gy;
                g_dirty = 1;
                g_need_rebuild = 1;
            } else if (ImGui::IsMouseDown(0)) {
                if (g_block_paint_active)
                    block_paint_line(im, g_block_paint_last_x, g_block_paint_last_y,
                                     gx, gy, (Uint8)g_block_edit_col);
                else
                    block_paint_pixel(im, gx, gy, (Uint8)g_block_edit_col);
                g_block_paint_active = true;
                g_block_paint_last_x = gx;
                g_block_paint_last_y = gy;
                g_dirty = 1;
                g_need_rebuild = 1;
            }
            if (ImGui::IsMouseClicked(1))
                g_block_edit_col = im->pix[gy * im->w + gx];

            dl->AddRect(ImVec2(origin.x + gx * zoom, origin.y + gy * zoom),
                        ImVec2(origin.x + (gx + 1) * zoom, origin.y + (gy + 1) * zoom),
                        IM_COL32(255, 255, 255, 220), 0, 0, 2.0f);
            Uint8 v = im->pix[gy * im->w + gx];
            ImGui::SetTooltip("x=%d y=%d index=%d", gx, gy, v);
        }
    }
    if (g_block_pixel_undo.active && !ImGui::IsMouseDown(0))
        block_pixel_undo_commit();
    if (!ImGui::IsMouseDown(0))
        g_block_paint_active = false;
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("block_controls", ImVec2(0, 0), true);

    ImGui::TextUnformatted("View");
    if (ImGui::SmallButton("-")) {
        g_block_edit_zoom--;
        g_block_edit_auto_fit = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(145.0f);
    if (ImGui::SliderInt("##block_zoom", &g_block_edit_zoom, 1, 64, "%dx"))
        g_block_edit_auto_fit = false;
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        g_block_edit_zoom++;
        g_block_edit_auto_fit = false;
    }
    block_editor_clamp_state(pc);

    if (ImGui::SmallButton("1:1")) {
        g_block_edit_zoom = 1;
        g_block_edit_auto_fit = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("8x")) {
        g_block_edit_zoom = 8;
        g_block_edit_auto_fit = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) {
        g_block_edit_zoom = fit_zoom;
        g_block_edit_auto_fit = true;
    }
    ImGui::Checkbox("Grid", &g_block_edit_grid);
    ImGui::TextDisabled("Ctrl+wheel zooms canvas");
    bool undo_disabled = !undo_is_available();
    if (undo_disabled) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Undo")) undo_restore();
    if (undo_disabled) ImGui::EndDisabled();
    ImGui::SameLine();
    bool redo_disabled = !redo_is_available();
    if (redo_disabled) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Redo")) redo_restore();
    if (redo_disabled) ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Transparent BG");
    block_editor_bg_option("Checker", 0, IM_COL32(0, 0, 0, 0));
    block_editor_bg_option("Hot pink", 1, IM_COL32(255, 0, 170, 255));
    block_editor_bg_option("Neon green", 2, IM_COL32(0, 255, 80, 255));
    block_editor_bg_option("Black", 3, IM_COL32(0, 0, 0, 255));
    block_editor_bg_option("White", 4, IM_COL32(255, 255, 255, 255));

    ImGui::Separator();
    ImGui::TextUnformatted("Brush");
    int edit_col = g_block_edit_col;
    ImGui::SetNextItemWidth(92.0f);
    if (ImGui::InputInt("Index", &edit_col)) {
        if (edit_col < 0) edit_col = 0;
        if (edit_col >= pc) edit_col = pc - 1;
        g_block_edit_col = edit_col;
    }
    Uint32 brush_c = pal ? pal[g_block_edit_col] : 0xFF000000u | ((Uint32)g_block_edit_col * 0x010101u);
    ImDrawList *ctl_dl = ImGui::GetWindowDrawList();
    ImVec2 brush_p = ImGui::GetCursorScreenPos();
    ImVec2 brush_q(brush_p.x + 36.0f, brush_p.y + 22.0f);
    if (g_block_edit_col == 0)
        block_editor_draw_checkerboard_background(brush_p, brush_q, 5.0f);
    else
        ctl_dl->AddRectFilled(brush_p, brush_q,
                              IM_COL32((brush_c >> 16) & 0xFF,
                                       (brush_c >> 8) & 0xFF,
                                       brush_c & 0xFF, 255));
    ctl_dl->AddRect(brush_p, brush_q, IM_COL32(180, 180, 190, 180));
    ImGui::InvisibleButton("##brush_color", ImVec2(36, 22));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(g_block_edit_col == 0 ? "0: transparent / erase"
                                                : "%d: #%02X%02X%02X",
                          g_block_edit_col, (brush_c >> 16) & 0xFF,
                          (brush_c >> 8) & 0xFF, brush_c & 0xFF);
    ImGui::SameLine();
    if (ImGui::SmallButton("Erase")) g_block_edit_col = 0;
    if (hover_x >= 0)
        ImGui::Text("Hover %d,%d", hover_x, hover_y);
    else
        ImGui::TextDisabled("Hover -, -");

    if (pal && im->pal_idx >= 0 && im->pal_idx < g_n_pals &&
        g_block_edit_col >= 0 && g_block_edit_col < pc) {
        Uint32 color = g_pals[im->pal_idx][g_block_edit_col];
        float edit_rgb[3] = {
            ((color >> 16) & 0xFF) / 255.0f,
            ((color >> 8) & 0xFF) / 255.0f,
            (color & 0xFF) / 255.0f
        };
        ImGui::SetNextItemWidth(-1);
        bool color_changed = ImGui::ColorEdit3("Color", edit_rgb,
            ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_DisplayRGB);
        bool color_activated = ImGui::IsItemActivated();
        bool color_deactivated = ImGui::IsItemDeactivatedAfterEdit();
        if (color_activated && !g_block_palette_color_undo.active)
            block_palette_undo_capture(&g_block_palette_color_undo,
                                       im->pal_idx, g_block_edit_col);
        if (color_changed) {
            if (!g_block_palette_color_undo.active)
                block_palette_undo_capture(&g_block_palette_color_undo,
                                           im->pal_idx, g_block_edit_col);
            Uint8 r = (Uint8)(edit_rgb[0] * 255.0f + 0.5f);
            Uint8 g_ = (Uint8)(edit_rgb[1] * 255.0f + 0.5f);
            Uint8 b = (Uint8)(edit_rgb[2] * 255.0f + 0.5f);
            Uint32 next = 0xFF000000u | ((Uint32)r << 16) |
                          ((Uint32)g_ << 8) | (Uint32)b;
            if (editor_project_set_palette_color(im->pal_idx, g_block_edit_col, next))
                block_palette_mark_changed();
        }
        if (color_deactivated && g_block_palette_color_undo.active)
            block_palette_undo_commit(&g_block_palette_color_undo,
                                      "Edit Palette Color");
        if (g_block_edit_col == 0)
            ImGui::TextDisabled("Index 0 renders transparent in the block.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Brightness / Contrast");
    bool tone_indexes[256];
    int tone_count = block_editor_collect_tone_indexes(im, pc,
                                                       g_block_tone_used_colors_only,
                                                       tone_indexes);
    bool can_tone = pal && im->pal_idx >= 0 && im->pal_idx < g_n_pals && tone_count > 0;
    if (!can_tone) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Used colors only", &g_block_tone_used_colors_only) &&
        g_block_palette_tone_undo.active)
        block_editor_apply_tone_from_capture(im, pc);

    bool tone_activated = false;
    bool tone_changed = false;
    bool tone_deactivated = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("Brightness", &g_block_tone_brightness,
                           -100.0f, 100.0f, "%.0f"))
        tone_changed = true;
    tone_activated = tone_activated || ImGui::IsItemActivated();
    tone_deactivated = tone_deactivated || ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("Contrast", &g_block_tone_contrast,
                           -100.0f, 100.0f, "%.0f"))
        tone_changed = true;
    tone_activated = tone_activated || ImGui::IsItemActivated();
    tone_deactivated = tone_deactivated || ImGui::IsItemDeactivatedAfterEdit();
    if (tone_activated && !g_block_palette_tone_undo.active)
        block_palette_undo_capture(&g_block_palette_tone_undo, im->pal_idx, -1);
    if (tone_changed) {
        if (!g_block_palette_tone_undo.active)
            block_palette_undo_capture(&g_block_palette_tone_undo, im->pal_idx, -1);
        block_editor_apply_tone_from_capture(im, pc);
    }
    if (tone_deactivated && g_block_palette_tone_undo.active)
        block_editor_finish_tone_edit();
    if (!can_tone) ImGui::EndDisabled();
    ImGui::TextDisabled("%d color(s) in this %dx%d block",
                        tone_count, im->w, im->h);

    ImGui::Separator();
    ImGui::Text("Palette %d", im->pal_idx);
    float pal_h = ImGui::GetContentRegionAvail().y;
    if (pal_h > 260.0f) pal_h = 260.0f;
    if (pal_h < 150.0f) pal_h = 150.0f;
    ImGui::BeginChild("block_palette", ImVec2(0, pal_h), true);
    float cbar_w = ImGui::GetContentRegionAvail().x;
    int cols = (int)(cbar_w / 24.0f);
    if (cols < 1) cols = 1;
    ImDrawList *pdl = ImGui::GetWindowDrawList();
    for (int i = 0; i < pc; i++) {
        Uint32 c = pal ? pal[i] : 0xFF000000u | ((Uint32)i * 0x010101u);
        ImU32 ic = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
        bool sel = (i == g_block_edit_col);
        if (i % cols != 0) ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushID(i);
        ImVec2 sp = ImGui::GetCursorScreenPos();
        if (i == 0)
            block_editor_draw_checkerboard_background(sp, ImVec2(sp.x + 20, sp.y + 20), 5.0f);
        else
            pdl->AddRectFilled(sp, ImVec2(sp.x + 20, sp.y + 20), ic);
        pdl->AddRect(sp, ImVec2(sp.x + 20, sp.y + 20),
                     sel ? IM_COL32(255, 255, 0, 255) : IM_COL32(80, 80, 90, 180),
                     0, 0, sel ? 2.0f : 1.0f);
        ImGui::InvisibleButton("##sw", ImVec2(20, 20));
        if (ImGui::IsItemClicked() || (ImGui::IsItemActive() && ImGui::IsItemHovered()))
            g_block_edit_col = i;
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%d: #%02X%02X%02X", i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            if (i == 0) ImGui::TextUnformatted("transparent");
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (g_block_match_ref_obj < 0 || g_block_match_ref_obj >= g_no ||
        (g_no > 1 && g_obj[g_block_match_ref_obj].ii == im->idx)) {
        g_block_match_ref_obj = -1;
        for (int i = 0; i < g_no; i++) {
            if (g_obj[i].ii == im->idx && g_no > 1) continue;
            g_block_match_ref_obj = i;
            break;
        }
    }

    if (ImGui::CollapsingHeader("Match Color / Style")) {
        char ref_preview[128] = "(none)";
        if (g_block_match_ref_obj >= 0 && g_block_match_ref_obj < g_no) {
            Img *rim = img_find(g_obj[g_block_match_ref_obj].ii);
            if (rim && rim->label[0])
                snprintf(ref_preview, sizeof ref_preview, "#%d  %s  pal %d",
                         g_block_match_ref_obj, rim->label, g_obj[g_block_match_ref_obj].fl);
            else if (rim)
                snprintf(ref_preview, sizeof ref_preview, "#%d  ii=0x%02X  %dx%d  pal %d",
                         g_block_match_ref_obj, g_obj[g_block_match_ref_obj].ii,
                         rim->w, rim->h, g_obj[g_block_match_ref_obj].fl);
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("Reference Object", ref_preview)) {
            for (int i = 0; i < g_no; i++) {
                Img *rim = img_find(g_obj[i].ii);
                if (!rim) continue;
                char lbl[160];
                if (rim->label[0])
                    snprintf(lbl, sizeof lbl, "#%d  %s  ii=0x%02X  pal %d%s",
                             i, rim->label, g_obj[i].ii, g_obj[i].fl,
                             g_obj[i].ii == im->idx ? "  (same image)" : "");
                else
                    snprintf(lbl, sizeof lbl, "#%d  ii=0x%02X  %dx%d  pal %d%s",
                             i, g_obj[i].ii, rim->w, rim->h, g_obj[i].fl,
                             g_obj[i].ii == im->idx ? "  (same image)" : "");
                if (ImGui::Selectable(lbl, i == g_block_match_ref_obj))
                    g_block_match_ref_obj = i;
            }
            ImGui::EndCombo();
        }
        bool can_use_sel = g_hl_obj >= 0 && g_hl_obj < g_no;
        if (!can_use_sel) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Use Selected"))
            g_block_match_ref_obj = g_hl_obj;
        if (!can_use_sel) ImGui::EndDisabled();

        Img *rim = (g_block_match_ref_obj >= 0 && g_block_match_ref_obj < g_no)
                 ? img_find(g_obj[g_block_match_ref_obj].ii) : NULL;
        if (rim) {
            static SDL_Texture *s_match_ref_tex = NULL;
            static int s_match_ref_frame = -1;
            static int s_match_ref_obj = -1;
            static int s_match_ref_w = 0;
            static int s_match_ref_h = 0;
            int frame = ImGui::GetFrameCount();
            if (s_match_ref_frame != frame || s_match_ref_obj != g_block_match_ref_obj) {
                if (s_match_ref_tex) {
                    SDL_DestroyTexture(s_match_ref_tex);
                    s_match_ref_tex = NULL;
                }
                s_match_ref_w = s_match_ref_h = 0;
                s_match_ref_tex = create_object_palette_preview_texture(g_block_match_ref_obj,
                                                                         &s_match_ref_w,
                                                                         &s_match_ref_h);
                s_match_ref_frame = frame;
                s_match_ref_obj = g_block_match_ref_obj;
            }
            if (s_match_ref_tex && s_match_ref_w > 0 && s_match_ref_h > 0) {
                int max_dim = s_match_ref_w > s_match_ref_h ? s_match_ref_w : s_match_ref_h;
                float sc = 140.0f / (float)max_dim;
                if (sc > 4.0f) sc = 4.0f;
                if (sc < 0.25f) sc = 0.25f;
                block_editor_draw_solid_backdrop_image(s_match_ref_tex,
                                                       ImVec2(s_match_ref_w * sc, s_match_ref_h * sc),
                                                       false);
            }
            ImGui::Text("Target 0x%02X -> ref 0x%02X", im->idx, rim->idx);
            ImGui::Text("Palette %d -> %d", im->pal_idx, g_obj[g_block_match_ref_obj].fl);
        }

        ImGui::Checkbox("Sample used colors only", &g_block_match_used_colors_only);
        ImGui::Checkbox("Apply palette to all uses", &g_block_match_all_uses);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Shade weight", &g_block_match_shade_weight, 0.0f, 1.0f, "%.2f");

        int cand_count = block_match_candidate_count(g_block_match_ref_obj,
                                                     g_block_match_used_colors_only);
        bool can_match = rim && cand_count > 0 &&
                         im->pal_idx >= 0 && im->pal_idx < g_n_pals &&
                         g_obj[g_block_match_ref_obj].fl >= 0 &&
                         g_obj[g_block_match_ref_obj].fl < g_n_pals;
        if (!can_match) ImGui::BeginDisabled();
        if (ImGui::Button("Match Active Block", ImVec2(-1, 0))) {
            int used_candidates = 0;
            int changed = block_match_image_to_object_style(g_block_edit_img,
                                                            g_block_match_ref_obj,
                                                            g_block_match_used_colors_only,
                                                            g_block_match_all_uses,
                                                            g_block_match_shade_weight,
                                                            &used_candidates);
            if (changed >= 0) {
                snprintf(g_block_match_status, sizeof g_block_match_status,
                         "Matched with %d reference color(s), changed %d value(s).",
                         used_candidates, changed);
                stage_set_toast("Matched block style");
            } else {
                snprintf(g_block_match_status, sizeof g_block_match_status,
                         "Could not match this block.");
            }
        }
        if (!can_match) ImGui::EndDisabled();
        ImGui::TextDisabled("%d reference color(s)", cand_count);
        if (g_block_match_status[0])
            ImGui::TextWrapped("%s", g_block_match_status);
    }

    ImGui::EndChild();
    ImGui::End();
}
