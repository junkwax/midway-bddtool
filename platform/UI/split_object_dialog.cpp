#include "bg_editor_globals.h"
#include "imgui.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static bool g_split_object_open = false;
static bool g_split_object_request = false;
static int  g_split_object_idx = -1;
static bool g_split_delete_source_image = true;
static bool g_split_compact_palettes = true;
static bool g_split_remove_unused_palettes = true;
static bool g_split_use_manual_tile = false;
static int  g_split_manual_tile_w = 32;
static int  g_split_manual_tile_h = 32;
static char g_split_status[256] = "";

struct SplitTileRect {
    int x, y, w, h;
};

struct ObjectSplitPlan {
    int obj_idx;
    int img_i;
    int tile_w, tile_h;
    int tile_count;
    int empty_tiles;
    int before_bytes;
    int after_bytes;
    int saved_bytes;
    int raw_after_pixels;
    int used_colors;
    int max_pixel;
    int palette_entries_saved;
    bool source_shared;
    bool valid;
    std::vector<SplitTileRect> rects;
};

static ObjectSplitPlan g_split_plan;
static ObjectSplitPlan g_split_best_plan;

static bool analyze_object_split(int obj_idx, int tile_w, int tile_h,
                                 bool trim_tiles, ObjectSplitPlan *out)
{
    if (!out) return false;
    ObjectSplitPlan plan = {};
    plan.obj_idx = obj_idx;
    plan.img_i = -1;
    plan.tile_w = tile_w;
    plan.tile_h = tile_h;
    if (obj_idx < 0 || obj_idx >= g_no || tile_w <= 0 || tile_h <= 0) {
        *out = plan;
        return false;
    }
    Img *im = img_find(g_obj[obj_idx].ii);
    if (!im || !im->pix || im->w <= 0 || im->h <= 0) {
        *out = plan;
        return false;
    }
    plan.img_i = (int)(im - g_img);
    plan.source_shared = image_use_count(im->idx) > 1;
    plan.before_bytes = im->w * im->h + 32;

    bool used[256];
    memset(used, 0, sizeof used);
    int max_px = 0;
    for (int i = 0; i < im->w * im->h; i++) {
        int v = im->pix[i];
        if (v <= 0) continue;
        used[v] = true;
        if (v > max_px) max_px = v;
    }
    for (int i = 1; i < 256; i++) if (used[i]) plan.used_colors++;
    plan.max_pixel = max_px;
    if (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
        plan.palette_entries_saved = g_pal_count[im->pal_idx] - (plan.used_colors + 1);
    if (plan.palette_entries_saved < 0) plan.palette_entries_saved = 0;

    int hfl = g_obj[obj_idx].hfl != 0;
    int vfl = g_obj[obj_idx].vfl != 0;
    for (int ty = 0; ty < im->h; ty += tile_h) {
        int th = tile_h;
        if (ty + th > im->h) th = im->h - ty;
        for (int tx = 0; tx < im->w; tx += tile_w) {
            int tw = tile_w;
            if (tx + tw > im->w) tw = im->w - tx;
            int minx = tw, miny = th, maxx = -1, maxy = -1;
            for (int yy = 0; yy < th; yy++) {
                int vy = ty + yy;
                int sy = vfl ? (im->h - 1 - vy) : vy;
                for (int xx = 0; xx < tw; xx++) {
                    int vx = tx + xx;
                    int sx = hfl ? (im->w - 1 - vx) : vx;
                    Uint8 px = im->pix[(size_t)sy * (size_t)im->w + (size_t)sx];
                    if (!px) continue;
                    if (xx < minx) minx = xx;
                    if (yy < miny) miny = yy;
                    if (xx > maxx) maxx = xx;
                    if (yy > maxy) maxy = yy;
                }
            }
            if (maxx < minx || maxy < miny) {
                plan.empty_tiles++;
                continue;
            }
            SplitTileRect r;
            r.x = tx + (trim_tiles ? minx : 0);
            r.y = ty + (trim_tiles ? miny : 0);
            r.w = trim_tiles ? (maxx - minx + 1) : tw;
            r.h = trim_tiles ? (maxy - miny + 1) : th;
            plan.rects.push_back(r);
            plan.tile_count++;
            plan.raw_after_pixels += r.w * r.h;
        }
    }

    plan.after_bytes = plan.raw_after_pixels + plan.tile_count * 32;
    plan.saved_bytes = plan.before_bytes - plan.after_bytes;
    plan.valid = plan.tile_count > 0;
    *out = plan;
    return plan.valid;
}

static bool find_best_object_split(int obj_idx, ObjectSplitPlan *out)
{
    static const int sizes[] = {16, 24, 32, 40, 48, 64, 80, 96, 128};
    bool found = false;
    ObjectSplitPlan best = {};
    for (int wi = 0; wi < (int)(sizeof sizes / sizeof sizes[0]); wi++) {
        for (int hi = 0; hi < (int)(sizeof sizes / sizeof sizes[0]); hi++) {
            ObjectSplitPlan cur;
            if (!analyze_object_split(obj_idx, sizes[wi], sizes[hi], true, &cur))
                continue;
            if (!found ||
                cur.saved_bytes > best.saved_bytes ||
                (cur.saved_bytes == best.saved_bytes && cur.tile_count < best.tile_count)) {
                best = cur;
                found = true;
            }
        }
    }
    if (found && out) *out = best;
    return found;
}

void open_split_object_dialog(int obj_idx)
{
    g_split_object_idx = obj_idx;
    g_split_status[0] = '\0';
    if (find_best_object_split(obj_idx, &g_split_best_plan)) {
        g_split_plan = g_split_best_plan;
        g_split_manual_tile_w = g_split_best_plan.tile_w;
        g_split_manual_tile_h = g_split_best_plan.tile_h;
        g_split_use_manual_tile = false;
    } else {
        g_split_plan = ObjectSplitPlan();
        snprintf(g_split_status, sizeof g_split_status, "No valid split found for this object.");
    }
    g_split_delete_source_image = true;
    g_split_compact_palettes = true;
    g_split_remove_unused_palettes = true;
    g_split_object_open = true;
    g_split_object_request = true;
}

static void draw_split_preview_overlay(const ObjectSplitPlan *plan, const Img *im, const Obj *o)
{
    if (!plan || !im || !o) return;
    SDL_Texture *tex = editor_texture_at(plan->img_i);
    float max_w = 360.0f, max_h = 260.0f;
    float scale = max_w / (float)im->w;
    if (scale > max_h / (float)im->h) scale = max_h / (float)im->h;
    if (scale > 3.0f) scale = 3.0f;
    if (scale < 0.1f) scale = 0.1f;
    ImVec2 size((float)im->w * scale, (float)im->h * scale);
    ImVec2 uv0(o->hfl ? 1.0f : 0.0f, o->vfl ? 1.0f : 0.0f);
    ImVec2 uv1(o->hfl ? 0.0f : 1.0f, o->vfl ? 0.0f : 1.0f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (tex)
        draw_editor_texture_transparent_uv(tex, size.x, size.y, uv0.x, uv0.y, uv1.x, uv1.y);
    else
        ImGui::Dummy(size);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 col = IM_COL32(255, 200, 70, 230);
    ImU32 fill = IM_COL32(255, 200, 70, 26);
    for (size_t i = 0; i < plan->rects.size(); i++) {
        const SplitTileRect &r = plan->rects[i];
        ImVec2 a(p0.x + r.x * scale, p0.y + r.y * scale);
        ImVec2 b(p0.x + (r.x + r.w) * scale, p0.y + (r.y + r.h) * scale);
        dl->AddRectFilled(a, b, fill);
        dl->AddRect(a, b, col);
    }
}

static void apply_object_split_plan(void)
{
    ObjectSplitPlan plan = g_split_plan;
    if (!plan.valid || plan.obj_idx < 0 || plan.obj_idx >= g_no ||
        plan.img_i < 0 || plan.img_i >= g_ni) {
        stage_set_toast("Split failed: stale object or image");
        return;
    }
    int old_img_idx = g_img[plan.img_i].idx;
    if (!editor_project_reserve_images(g_ni + plan.tile_count) ||
        !editor_project_reserve_objects(g_no + plan.tile_count)) {
        stage_set_toast("Split failed: could not reserve image/object slots");
        return;
    }
    g_chop_tile_w = plan.tile_w;
    g_chop_tile_h = plan.tile_h;
    g_chop_trim_tiles = true;
    int added = chop_image_to_map(plan.img_i, g_obj[plan.obj_idx].depth, g_obj[plan.obj_idx].sy,
                                  g_obj[plan.obj_idx].wx, g_obj[plan.obj_idx].fl,
                                  g_obj[plan.obj_idx].hfl != 0, g_obj[plan.obj_idx].vfl != 0,
                                  plan.obj_idx, true);
    if (added <= 0) {
        stage_set_toast("Split produced no replacement tiles");
        return;
    }

    int removed_images = 0;
    if (g_split_delete_source_image) {
        for (int i = 0; i < g_ni; i++) {
            if (g_img[i].idx == old_img_idx) {
                removed_images = delete_image_slot_if_unused(i);
                break;
            }
        }
    }
    int compacted = 0;
    if (g_split_compact_palettes)
        compacted = compact_palettes_for_image_range(0, g_ni, false);
    int removed_pals = 0;
    if (g_split_remove_unused_palettes)
        removed_pals = remove_unused_palettes_impl(false);

    g_mk2_palette_sync_dirty = g_mk2_palette_sync_dirty || compacted > 0 || removed_pals > 0;
    g_split_object_open = false;
    g_split_object_idx = -1;
    char msg[192];
    int net_est = removed_images ? plan.saved_bytes : -plan.after_bytes;
    snprintf(msg, sizeof msg,
             "Split into %d tile(s), est %s%d bytes, compacted %d pal, removed %d pal",
             added, net_est >= 0 ? "saved " : "added ", net_est >= 0 ? net_est : -net_est,
             compacted, removed_pals);
    stage_set_toast(msg);
}

void draw_split_object_dialog(void)
{
    if (g_split_object_request) {
        ImGui::OpenPopup("Split Object");
        g_split_object_request = false;
    }
    if (!g_split_object_open) return;

    bool modal_open = true;
    ImGui::SetNextWindowSize(ImVec2(680, 560), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Split Object", &modal_open)) {
        int obj_idx = g_split_object_idx;
        Obj *o = (obj_idx >= 0 && obj_idx < g_no) ? &g_obj[obj_idx] : NULL;
        Img *im = o ? img_find(o->ii) : NULL;
        if (!o || !im) {
            ImGui::TextUnformatted("Object is no longer available.");
        } else {
            if (g_split_use_manual_tile) {
                if (g_split_manual_tile_w < 4) g_split_manual_tile_w = 4;
                if (g_split_manual_tile_h < 4) g_split_manual_tile_h = 4;
                analyze_object_split(obj_idx, g_split_manual_tile_w, g_split_manual_tile_h,
                                     true, &g_split_plan);
            } else {
                g_split_plan = g_split_best_plan;
            }

            ImGui::Text("Object %d  image 0x%04X  %dx%d", obj_idx, o->ii, im->w, im->h);
            if (g_split_best_plan.valid)
                ImGui::TextColored(ImVec4(0.45f,1.0f,0.55f,1.0f),
                    "Best: %dx%d -> %d tile(s), est save %d bytes",
                    g_split_best_plan.tile_w, g_split_best_plan.tile_h,
                    g_split_best_plan.tile_count, g_split_best_plan.saved_bytes);
            if (g_split_status[0])
                ImGui::TextWrapped("%s", g_split_status);
            ImGui::Separator();

            ImGui::Checkbox("Manual tile size", &g_split_use_manual_tile);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            ImGui::InputInt("W", &g_split_manual_tile_w);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            ImGui::InputInt("H", &g_split_manual_tile_h);
            ImGui::Checkbox("Delete original source image if unused", &g_split_delete_source_image);
            ImGui::Checkbox("Compact palettes after split", &g_split_compact_palettes);
            ImGui::SameLine();
            ImGui::Checkbox("Remove unused palettes", &g_split_remove_unused_palettes);

            ImGui::Separator();
            if (g_split_plan.valid) {
                int ref_count = image_use_count(im->idx);
                int net_est = (g_split_delete_source_image && ref_count <= 1)
                            ? g_split_plan.saved_bytes
                            : -g_split_plan.after_bytes;
                ImGui::Text("Selected split: %dx%d, %d tile(s), %d empty tile(s)",
                            g_split_plan.tile_w, g_split_plan.tile_h,
                            g_split_plan.tile_count, g_split_plan.empty_tiles);
                ImGui::Text("Image bytes: before %d, after %d, replace saving %d",
                            g_split_plan.before_bytes, g_split_plan.after_bytes,
                            g_split_plan.saved_bytes);
                if (ref_count > 1)
                    ImGui::TextColored(ImVec4(1.0f,0.75f,0.25f,1.0f),
                        "Source image has %d placements; keeping it means this split adds about %d bytes.",
                        ref_count, g_split_plan.after_bytes);
                else
                    ImGui::Text("Net estimate with current options: %s%d bytes",
                                net_est >= 0 ? "save " : "add ",
                                net_est >= 0 ? net_est : -net_est);
                ImGui::Text("Palette: %d used color(s), max index %d, compact can free up to %d entries",
                            g_split_plan.used_colors, g_split_plan.max_pixel,
                            g_split_plan.palette_entries_saved);
                draw_split_preview_overlay(&g_split_plan, im, o);
            } else {
                ImGui::TextColored(ImVec4(1.0f,0.35f,0.25f,1.0f),
                    "This split cannot fit current image/object limits.");
            }
        }
        ImGui::Separator();
        bool can_apply = g_split_plan.valid && o && im;
        if (!can_apply) ImGui::BeginDisabled();
        if (ImGui::Button("OK - Replace Object", ImVec2(170, 0)))
            apply_object_split_plan();
        if (!can_apply) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) {
            g_split_object_open = false;
            g_split_object_idx = -1;
        }
        ImGui::EndPopup();
    }
    if (!modal_open) {
        g_split_object_open = false;
        g_split_object_idx = -1;
    }
}
