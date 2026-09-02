#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

/* Payload accounting mirrors mk2_collect_budget(): each image costs a 12-byte
   header plus its packed block, each placement costs an 8-byte object record.
   A split trades one block for several and one placement for several, so both
   halves have to be in the estimate or the dialog promises savings it cannot
   deliver. */
static const int SPLIT_IMAGE_HEADER_BYTES = 12;
static const int SPLIT_OBJECT_BYTES = 8;

static bool g_split_object_open = false;
static bool g_split_object_request = false;
static int  g_split_object_idx = -1;
static bool g_split_delete_source_image = true;
static bool g_split_compact_palettes = true;
static bool g_split_remove_unused_palettes = true;
static bool g_split_all_placements = true;
static bool g_split_use_manual_tile = false;
static int  g_split_manual_tile_w = 32;
static int  g_split_manual_tile_h = 32;
static char g_split_status[256] = "";

struct SplitTileRect {
    int x, y, w, h;   /* rect in unflipped source-image pixels */
    int max_pixel;
    int bytes;        /* packed LOAD2 block bytes for this tile */
};

struct ObjectSplitPlan {
    int obj_idx;
    int img_i;
    int src_w, src_h;
    int tile_w, tile_h;
    int tile_count;
    int empty_tiles;
    int total_uses;         /* placements of the source image in the stage */
    int placements;         /* placements this plan rebuilds */
    int src_image_bytes;    /* source block + header */
    int tile_image_bytes;   /* all tile blocks + headers */
    int obj_bytes_before;
    int obj_bytes_after;
    int raw_after_pixels;
    int used_colors;
    int max_pixel;
    int palette_entries_saved;
    bool valid;
    std::vector<SplitTileRect> rects;
};

static ObjectSplitPlan g_split_plan;
static ObjectSplitPlan g_split_best_plan;

/* Bytes the stage gains (positive) by applying the plan. The source block only
   comes back when every placement of it was rebuilt. */
static int split_plan_net_bytes(const ObjectSplitPlan &plan, bool delete_source)
{
    int net = plan.obj_bytes_before - plan.obj_bytes_after - plan.tile_image_bytes;
    if (delete_source && plan.placements >= plan.total_uses)
        net += plan.src_image_bytes;
    return net;
}

static bool split_plan_frees_source(const ObjectSplitPlan &plan)
{
    return g_split_delete_source_image && plan.placements >= plan.total_uses;
}

/* Tiles are always cut from the unflipped source, so one tile set serves every
   placement: a mirrored or flipped placement re-uses the same tiles with its
   own flip flags and mirrored offsets. */
static bool analyze_object_split(int obj_idx, int tile_w, int tile_h,
                                 bool trim_tiles, bool all_placements,
                                 ObjectSplitPlan *out)
{
    if (!out) return false;
    ObjectSplitPlan plan = ObjectSplitPlan();
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
    plan.src_w = im->w;
    plan.src_h = im->h;
    plan.total_uses = image_use_count(im->idx);
    if (plan.total_uses < 1) plan.total_uses = 1;
    plan.placements = all_placements ? plan.total_uses : 1;
    plan.src_image_bytes = (int)mk2_estimate_image_bytes(im) + SPLIT_IMAGE_HEADER_BYTES;

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

    for (int ty = 0; ty < im->h; ty += tile_h) {
        int th = tile_h;
        if (ty + th > im->h) th = im->h - ty;
        for (int tx = 0; tx < im->w; tx += tile_w) {
            int tw = tile_w;
            if (tx + tw > im->w) tw = im->w - tx;
            int minx = tw, miny = th, maxx = -1, maxy = -1;
            int tile_max_px = 0;
            for (int yy = 0; yy < th; yy++) {
                for (int xx = 0; xx < tw; xx++) {
                    Uint8 px = im->pix[(size_t)(ty + yy) * (size_t)im->w + (size_t)(tx + xx)];
                    if (!px) continue;
                    if (xx < minx) minx = xx;
                    if (yy < miny) miny = yy;
                    if (xx > maxx) maxx = xx;
                    if (yy > maxy) maxy = yy;
                    if (px > tile_max_px) tile_max_px = px;
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
            r.max_pixel = tile_max_px;
            r.bytes = (int)(((size_t)r.w * (size_t)r.h *
                             (size_t)mk2_bpp_for_max_index(tile_max_px) + 7u) / 8u);
            plan.rects.push_back(r);
            plan.tile_count++;
            plan.raw_after_pixels += r.w * r.h;
            plan.tile_image_bytes += r.bytes + SPLIT_IMAGE_HEADER_BYTES;
        }
    }

    plan.obj_bytes_before = plan.placements * SPLIT_OBJECT_BYTES;
    plan.obj_bytes_after = plan.placements * plan.tile_count * SPLIT_OBJECT_BYTES;
    plan.valid = plan.tile_count > 0;
    *out = plan;
    return plan.valid;
}

static bool find_best_object_split(int obj_idx, bool all_placements, ObjectSplitPlan *out)
{
    static const int sizes[] = {16, 24, 32, 40, 48, 64, 80, 96, 128};
    bool found = false;
    ObjectSplitPlan best = ObjectSplitPlan();
    int best_net = 0;
    for (int wi = 0; wi < (int)(sizeof sizes / sizeof sizes[0]); wi++) {
        for (int hi = 0; hi < (int)(sizeof sizes / sizeof sizes[0]); hi++) {
            ObjectSplitPlan cur;
            if (!analyze_object_split(obj_idx, sizes[wi], sizes[hi], true,
                                      all_placements, &cur))
                continue;
            int net = split_plan_net_bytes(cur, true);
            if (!found || net > best_net ||
                (net == best_net && cur.tile_count < best.tile_count)) {
                best = cur;
                best_net = net;
                found = true;
            }
        }
    }
    if (found && out) *out = best;
    return found;
}

static void refresh_best_object_split(void)
{
    if (find_best_object_split(g_split_object_idx, g_split_all_placements,
                               &g_split_best_plan)) {
        g_split_manual_tile_w = g_split_best_plan.tile_w;
        g_split_manual_tile_h = g_split_best_plan.tile_h;
        g_split_status[0] = '\0';
    } else {
        g_split_best_plan = ObjectSplitPlan();
        snprintf(g_split_status, sizeof g_split_status, "No valid split found for this object.");
    }
}

void open_split_object_dialog(int obj_idx)
{
    g_split_object_idx = obj_idx;
    g_split_status[0] = '\0';
    g_split_all_placements = true;
    g_split_use_manual_tile = false;
    refresh_best_object_split();
    g_split_plan = g_split_best_plan;
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
    /* rects are source-space; the preview shows this placement, so mirror them
       the same way the placement mirrors the image. */
    for (size_t i = 0; i < plan->rects.size(); i++) {
        const SplitTileRect &r = plan->rects[i];
        int vx = o->hfl ? (im->w - r.x - r.w) : r.x;
        int vy = o->vfl ? (im->h - r.y - r.h) : r.y;
        ImVec2 a(p0.x + vx * scale, p0.y + vy * scale);
        ImVec2 b(p0.x + (vx + r.w) * scale, p0.y + (vy + r.h) * scale);
        dl->AddRectFilled(a, b, fill);
        dl->AddRect(a, b, col);
    }
}

/* Applies a plan to the project. Returns false and fills msg with the reason on
   failure; on success msg holds the summary the caller reports. */
static bool split_object_apply_plan(const ObjectSplitPlan &plan, bool all_placements,
                                    bool delete_source, bool compact_pals, bool remove_pals,
                                    char *msg, size_t msgsz)
{
    if (!plan.valid || plan.obj_idx < 0 || plan.obj_idx >= g_no ||
        plan.img_i < 0 || plan.img_i >= g_ni) {
        snprintf(msg, msgsz, "Split failed: stale object or image");
        return false;
    }
    Img *src = &g_img[plan.img_i];
    if (!src->pix || src->w != plan.src_w || src->h != plan.src_h) {
        snprintf(msg, msgsz, "Split failed: source image changed");
        return false;
    }

    /* Snapshot the source before touching storage: appending image or object
       slots can reallocate g_img/g_obj out from under these pointers. */
    const int old_img_idx = src->idx;
    const int sw = src->w, sh = src->h;
    std::vector<Uint8> src_pix(src->pix, src->pix + (size_t)sw * (size_t)sh);
    const int src_flags = src->flags & 1;
    const int src_pal = (src->pal_idx >= 0 && src->pal_idx < g_n_pals) ? src->pal_idx : 0;
    char src_label[64];
    snprintf(src_label, sizeof src_label, "%s", src->label[0] ? src->label : "CHOP");
    char src_source[64];
    snprintf(src_source, sizeof src_source, "%s",
             src->source[0] ? src->source : (src->label[0] ? src->label : "BDD"));

    std::vector<int> targets;
    std::vector<Obj> target_obj;
    std::vector<int> target_hidden, target_lock;
    for (int i = 0; i < g_no; i++) {
        if (g_obj[i].ii != old_img_idx) continue;
        if (!all_placements && i != plan.obj_idx) continue;
        targets.push_back(i);
        target_obj.push_back(g_obj[i]);
        target_hidden.push_back(g_obj_hidden ? g_obj_hidden[i] : 0);
        target_lock.push_back(g_obj_lock ? g_obj_lock[i] : 0);
    }
    if (targets.empty()) {
        snprintf(msg, msgsz, "Split failed: no placement uses this image");
        return false;
    }

    const int wanted_objects = (int)targets.size() * plan.tile_count;
    if (!editor_project_reserve_images(g_ni + plan.tile_count) ||
        !editor_project_reserve_objects(g_no + wanted_objects)) {
        snprintf(msg, msgsz, "Split failed: could not reserve image/object slots");
        return false;
    }

    undo_save_ex("Split Object");

    /* One tile set, cut unflipped, shared by every placement. */
    std::vector<int> tile_idx(plan.rects.size(), -1);
    int created_images = 0;
    for (size_t t = 0; t < plan.rects.size(); t++) {
        const SplitTileRect &r = plan.rects[t];
        Uint8 *pix = (Uint8 *)malloc((size_t)r.w * (size_t)r.h);
        if (!pix) continue;
        for (int yy = 0; yy < r.h; yy++)
            memcpy(pix + (size_t)yy * (size_t)r.w,
                   &src_pix[(size_t)(r.y + yy) * (size_t)sw + (size_t)r.x],
                   (size_t)r.w);
        int new_idx = next_free_image_index(old_img_idx + created_images + 1);
        if (new_idx < 0) { free(pix); continue; }
        Img *dst = editor_project_append_image_slot();
        if (!dst) { free(pix); continue; }
        dst->idx = new_idx;
        dst->w = r.w;
        dst->h = r.h;
        dst->flags = src_flags;
        dst->pal_idx = src_pal;
        dst->pix = pix;
        snprintf(dst->label, sizeof dst->label, "%.40s_%02d_%02d",
                 src_label, r.x / plan.tile_w, r.y / plan.tile_h);
        snprintf(dst->source, sizeof dst->source, "%.40s_CHOP", src_source);
        tile_idx[t] = new_idx;
        created_images++;
    }
    if (created_images == 0) {
        snprintf(msg, msgsz, "Split produced no replacement tiles");
        return false;
    }

    /* Rebuild every placement from the shared tiles. A placement keeps its own
       layer, palette, draw order and flip flags; only the tile offsets are
       mirrored, so a mirrored/flipped/rotated placement still looks exactly as
       it did. */
    const std::vector<SplitTileRect> &rects = plan.rects;
    std::vector<int> emit;
    int added_objects = 0;
    for (size_t p = 0; p < target_obj.size(); p++) {
        const Obj po = target_obj[p];
        const int hfl = po.hfl != 0, vfl = po.vfl != 0;
        emit.clear();
        for (size_t t = 0; t < tile_idx.size(); t++)
            if (tile_idx[t] >= 0) emit.push_back((int)t);
        /* LOAD2 walks a module's blocks in draw order and expects world X never
           to step back, so emit each placement's tiles left to right as that
           placement actually shows them. Split tiles never overlap each other,
           so re-ordering them cannot change what is drawn. */
        std::stable_sort(emit.begin(), emit.end(),
                         [&rects, hfl, vfl, sw, sh](int a, int b) {
            int ax = hfl ? (sw - rects[(size_t)a].x - rects[(size_t)a].w) : rects[(size_t)a].x;
            int bx = hfl ? (sw - rects[(size_t)b].x - rects[(size_t)b].w) : rects[(size_t)b].x;
            if (ax != bx) return ax < bx;
            int ay = vfl ? (sh - rects[(size_t)a].y - rects[(size_t)a].h) : rects[(size_t)a].y;
            int by = vfl ? (sh - rects[(size_t)b].y - rects[(size_t)b].h) : rects[(size_t)b].y;
            return ay < by;
        });
        for (size_t e = 0; e < emit.size(); e++) {
            const SplitTileRect &r = rects[(size_t)emit[e]];
            Obj *o = editor_project_append_object_slot();
            if (!o) break;
            int obj_i = g_no - 1;
            o->wx = (po.wx & ~0x30) | (hfl ? 0x10 : 0) | (vfl ? 0x20 : 0);
            o->depth = po.depth + (hfl ? (sw - r.x - r.w) : r.x);
            o->sy = po.sy + (vfl ? (sh - r.y - r.h) : r.y);
            o->ii = tile_idx[(size_t)emit[e]];
            o->fl = po.fl;
            o->hfl = hfl;
            o->vfl = vfl;
            /* Same order value as the placement it replaces: the layer/order
               sorts are stable, so the tile run lands in the slot the original
               object held instead of jumping to the top of the layer. */
            o->order = po.order;
            if (g_obj_hidden) g_obj_hidden[obj_i] = target_hidden[p];
            if (g_obj_lock) g_obj_lock[obj_i] = target_lock[p];
            if (g_sel_flags) g_sel_flags[obj_i] = 1;
            simple_ensure_module(obj_i);
            added_objects++;
        }
    }
    if (added_objects == 0) {
        snprintf(msg, msgsz, "Split produced no replacement placements");
        return false;
    }

    for (int i = (int)targets.size() - 1; i >= 0; i--)
        editor_project_delete_object_slot(targets[(size_t)i]);

    if (g_sel_flags) {
        for (int i = 0; i < g_no - added_objects; i++) g_sel_flags[i] = 0;
        for (int i = g_no - added_objects; i < g_no; i++)
            if (i >= 0) g_sel_flags[i] = 1;
    }
    g_hl_obj = g_no > 0 ? g_no - 1 : -1;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;

    int removed_images = 0;
    if (delete_source) {
        for (int i = 0; i < g_ni; i++) {
            if (g_img[i].idx == old_img_idx) {
                removed_images = delete_image_slot_if_unused(i);
                break;
            }
        }
    }
    int compacted = 0;
    if (compact_pals)
        compacted = compact_palettes_for_image_range(0, g_ni, false);
    int removed_pals = 0;
    if (remove_pals)
        removed_pals = remove_unused_palettes_impl(false);

    g_mk2_palette_sync_dirty = g_mk2_palette_sync_dirty || compacted > 0 || removed_pals > 0;

    int net_est = ((int)targets.size() - added_objects) * SPLIT_OBJECT_BYTES
                - plan.tile_image_bytes;
    if (removed_images) net_est += plan.src_image_bytes;
    snprintf(msg, msgsz,
             "Split %d placement(s) into %d shared tile(s), est %s%d bytes, compacted %d pal, removed %d pal",
             (int)targets.size(), created_images,
             net_est >= 0 ? "saved " : "added ", net_est >= 0 ? net_est : -net_est,
             compacted, removed_pals);
    return true;
}

static void apply_object_split_plan(void)
{
    char msg[224];
    bool ok = split_object_apply_plan(g_split_plan, g_split_all_placements,
                                      g_split_delete_source_image,
                                      g_split_compact_palettes,
                                      g_split_remove_unused_palettes,
                                      msg, sizeof msg);
    if (ok) {
        g_split_object_open = false;
        g_split_object_idx = -1;
    }
    stage_set_toast(msg);
}

/* Programmatic split, used by the --split-object-smoke CLI check. Plans the
   object at the given tile size and applies it, always deleting the freed
   source image. Palette compaction is opt-in so the check can measure the split
   on its own. */
int split_object_at_tile_size(int obj_idx, int tile_w, int tile_h, int all_placements,
                              int compact_palettes)
{
    ObjectSplitPlan plan;
    if (!analyze_object_split(obj_idx, tile_w, tile_h, true, all_placements != 0, &plan))
        return 0;
    char msg[224];
    return split_object_apply_plan(plan, all_placements != 0, true,
                                   compact_palettes != 0, compact_palettes != 0,
                                   msg, sizeof msg) ? 1 : 0;
}

void draw_split_object_dialog(void)
{
    if (g_split_object_request) {
        ImGui::OpenPopup("Split Object");
        g_split_object_request = false;
    }
    if (!g_split_object_open) return;

    bool modal_open = true;
    ImGui::SetNextWindowSize(ImVec2(680, 600), ImGuiCond_FirstUseEver);
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
                                     true, g_split_all_placements, &g_split_plan);
            } else {
                g_split_plan = g_split_best_plan;
            }

            int uses = image_use_count(im->idx);
            ImGui::Text("Object %d  image 0x%04X  %dx%d  %d placement(s)",
                        obj_idx, o->ii, im->w, im->h, uses);
            if (o->hfl || o->vfl)
                ImGui::TextDisabled("This placement is %s; tiles are cut from the unflipped source.",
                                    (o->hfl && o->vfl) ? "rotated 180" :
                                    (o->hfl ? "mirrored" : "flipped"));
            if (g_split_best_plan.valid)
                ImGui::TextColored(ImVec4(0.45f,1.0f,0.55f,1.0f),
                    "Best: %dx%d -> %d tile(s), est save %d bytes",
                    g_split_best_plan.tile_w, g_split_best_plan.tile_h,
                    g_split_best_plan.tile_count,
                    split_plan_net_bytes(g_split_best_plan, true));
            if (g_split_status[0])
                ImGui::TextWrapped("%s", g_split_status);
            ImGui::Separator();

            if (ImGui::Checkbox("Split every placement of this image", &g_split_all_placements)) {
                refresh_best_object_split();
                if (!g_split_use_manual_tile) g_split_plan = g_split_best_plan;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "All %d placement(s) are rebuilt from one shared tile set.\n"
                    "Mirrored, flipped and rotated placements keep their form:\n"
                    "they re-use the same tiles with their own flip flags.\n"
                    "Leaving a placement behind keeps the source image alive,\n"
                    "so the split adds bytes instead of saving them.", uses);

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
                bool frees_source = split_plan_frees_source(g_split_plan);
                int net_est = split_plan_net_bytes(g_split_plan, g_split_delete_source_image);
                int left_behind = g_split_plan.total_uses - g_split_plan.placements;
                ImGui::Text("Selected split: %dx%d, %d tile(s), %d empty tile(s), %d placement(s) rebuilt",
                            g_split_plan.tile_w, g_split_plan.tile_h,
                            g_split_plan.tile_count, g_split_plan.empty_tiles,
                            g_split_plan.placements);
                ImGui::Text("Image bytes: source %d, tiles %d",
                            g_split_plan.src_image_bytes, g_split_plan.tile_image_bytes);
                ImGui::Text("Object bytes: before %d, after %d",
                            g_split_plan.obj_bytes_before, g_split_plan.obj_bytes_after);
                if (!frees_source) {
                    if (left_behind > 0)
                        ImGui::TextColored(ImVec4(1.0f,0.75f,0.25f,1.0f),
                            "%d placement(s) keep using the source image, so it stays in the "
                            "stage and this split adds about %d bytes.",
                            left_behind, -net_est);
                    else
                        ImGui::TextColored(ImVec4(1.0f,0.75f,0.25f,1.0f),
                            "The source image is kept, so this split adds about %d bytes.",
                            -net_est);
                }
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
