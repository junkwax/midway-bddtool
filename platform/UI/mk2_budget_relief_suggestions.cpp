#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

enum Mk2ReliefKind {
    MK2_RELIEF_DELETE_UNUSED = 0,
    MK2_RELIEF_REMOVE_SPRITE,
    MK2_RELIEF_TRIM_BORDER,
    MK2_RELIEF_COMPRESS_PALETTE,
    MK2_RELIEF_REPLACE_LOWER_COLOR
};

struct Mk2ReliefCandidate {
    int img_i;
    int kind;
    int uses;
    int bpp;
    int max_idx;
    int used_colors;
    int target_colors;
    size_t recover;
};

static const char *mk2_relief_action_name(int kind)
{
    switch (kind) {
        case MK2_RELIEF_DELETE_UNUSED:      return "delete";
        case MK2_RELIEF_REMOVE_SPRITE:      return "remove";
        case MK2_RELIEF_TRIM_BORDER:        return "trim";
        case MK2_RELIEF_COMPRESS_PALETTE:   return "compress";
        case MK2_RELIEF_REPLACE_LOWER_COLOR:return "replace";
        default:                            return "inspect";
    }
}

static void mk2_relief_candidate_reason(const Img *im, const Mk2ReliefCandidate *c,
                                        char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!im || !c) return;
    if (im->lod_ref || im->source[0])
        stage_append(out, outsz, "new/LOD ");
    if (c->kind == MK2_RELIEF_DELETE_UNUSED)
        stage_append(out, outsz, "unused art ");
    else if (c->kind == MK2_RELIEF_REMOVE_SPRITE)
        stage_append(out, outsz, "large placed sprite ");
    else if (c->kind == MK2_RELIEF_TRIM_BORDER)
        stage_append(out, outsz, "transparent border ");
    else if (c->kind == MK2_RELIEF_COMPRESS_PALETTE)
        stage_append(out, outsz, "few colors, high indexes ");
    else if (c->kind == MK2_RELIEF_REPLACE_LOWER_COLOR)
        stage_append(out, outsz, "requantize/reimport lower color ");
    if (c->max_idx >= 64)
        stage_append(out, outsz, "high-color ");
    if (!out[0])
        stage_append(out, outsz, "inspect");
}

static void mk2_add_relief_candidate(std::vector<Mk2ReliefCandidate> *out,
                                     const Mk2ReliefCandidate *cand)
{
    if (!out || !cand || cand->img_i < 0 || cand->img_i >= g_ni || cand->recover == 0)
        return;
    out->push_back(*cand);
}

static void mk2_collect_budget_relief_candidates(std::vector<Mk2ReliefCandidate> *out)
{
    if (!out) return;
    out->clear();

    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (!im->pix || im->w <= 0 || im->h <= 0) continue;

        int uses = image_use_count(im->idx);
        if ((uses == 0 && !g_budget_relief_show_unused) ||
            (uses > 0 && !g_budget_relief_show_used))
            continue;

        int used_colors = 0, max_idx = 0;
        image_palette_usage_stats(im, &used_colors, &max_idx);
        int bpp = mk2_bpp_for_image(im);
        size_t raw = mk2_estimate_image_bytes(im);

        Mk2ReliefCandidate c;
        memset(&c, 0, sizeof c);
        c.img_i = i;
        c.uses = uses;
        c.bpp = bpp;
        c.max_idx = max_idx;
        c.used_colors = used_colors;

        if (uses == 0) {
            c.kind = MK2_RELIEF_DELETE_UNUSED;
            c.recover = raw + 12u;
            mk2_add_relief_candidate(out, &c);
        } else {
            c.kind = MK2_RELIEF_REMOVE_SPRITE;
            c.recover = raw + 12u + (size_t)uses * 8u;
            mk2_add_relief_candidate(out, &c);
        }

        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (image_nonzero_bounds(im, &x1, &y1, &x2, &y2)) {
            int nw = x2 - x1 + 1;
            int nh = y2 - y1 + 1;
            size_t trimmed = ((size_t)nw * (size_t)nh * (size_t)bpp + 7u) / 8u;
            if (raw > trimmed) {
                c.kind = MK2_RELIEF_TRIM_BORDER;
                c.recover = raw - trimmed;
                c.target_colors = 0;
                mk2_add_relief_candidate(out, &c);
            }
        }

        int target = 0;
        if (used_colors > 0 && used_colors <= 15 && max_idx > 15)
            target = 15;
        else if (used_colors > 0 && used_colors <= 31 && max_idx > 31)
            target = 31;
        else if (used_colors > 0 && used_colors <= 63 && max_idx > 63)
            target = 63;
        if (target > 0) {
            size_t compact = mk2_estimate_image_bytes_for_bpp(im, mk2_bpp_for_max_index(target));
            if (raw > compact) {
                c.kind = MK2_RELIEF_COMPRESS_PALETTE;
                c.recover = raw - compact;
                c.target_colors = target;
                mk2_add_relief_candidate(out, &c);
            }
        } else if (max_idx >= 64) {
            size_t compact = mk2_estimate_image_bytes_for_bpp(im, 6);
            if (raw > compact) {
                c.kind = MK2_RELIEF_REPLACE_LOWER_COLOR;
                c.recover = raw - compact;
                c.target_colors = 63;
                mk2_add_relief_candidate(out, &c);
            }
        }
    }

    std::stable_sort(out->begin(), out->end(), [](const Mk2ReliefCandidate &a,
                                                  const Mk2ReliefCandidate &b) {
        if (a.recover != b.recover) return a.recover > b.recover;
        if (a.uses != b.uses) return a.uses < b.uses;
        return g_img[a.img_i].idx < g_img[b.img_i].idx;
    });
}

static void mk2_focus_budget_relief_image(int img_i)
{
    if (img_i < 0 || img_i >= g_ni) return;
    Img *im = &g_img[img_i];
    g_budget_relief_highlight_img_ii = im->idx;
    g_tile_img = img_i;
    g_place_tool_img = img_i;
    g_show_images = true;

    int selected = mk2_select_objects_by_image(im->idx);
    if (selected > 0 && g_hl_obj >= 0 && g_hl_obj < g_no) {
        g_view_x = g_obj[g_hl_obj].depth - 160;
        g_view_y = g_obj[g_hl_obj].sy - 96;
        g_view_changed = 1;
    } else {
        snprintf(g_obj_filter, sizeof g_obj_filter, "%04X", im->idx);
    }
}

static int mk2_clear_image_uses(int image_idx, const char *undo_label)
{
    int uses = image_use_count(image_idx);
    if (uses <= 0) return 0;
    undo_save_ex(undo_label ? undo_label : "Clear Sprite Uses");
    int removed = 0;
    for (int i = g_no - 1; i >= 0; i--) {
        if (g_obj[i].ii != image_idx) continue;
        mk2_delete_object_preserve_order(i);
        removed++;
    }
    editor_project_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;
    return removed;
}

static int mk2_delete_image_slot(int img_i)
{
    if (img_i < 0 || img_i >= g_ni) return 0;
    if (!editor_project_delete_image_slot(img_i)) return 0;
    if (g_tile_img >= g_ni) g_tile_img = g_ni - 1;
    if (g_place_tool_img >= g_ni) g_place_tool_img = g_ni - 1;
    if (g_tile_img < 0 && g_ni > 0) g_tile_img = 0;
    return 1;
}

static int mk2_remove_image_and_uses(int img_i)
{
    if (img_i < 0 || img_i >= g_ni) return 0;
    int image_idx = g_img[img_i].idx;
    undo_save_ex("Remove Budget Sprite");
    int removed_uses = 0;
    for (int i = g_no - 1; i >= 0; i--) {
        if (g_obj[i].ii != image_idx) continue;
        mk2_delete_object_preserve_order(i);
        removed_uses++;
    }
    int removed_image = mk2_delete_image_slot(img_i);
    editor_project_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_need_rebuild = 1;

    char msg[128];
    snprintf(msg, sizeof msg, "Removed image 0x%02X and %d placement(s)", image_idx, removed_uses);
    stage_set_toast(removed_image ? msg : "Sprite removal failed");
    return removed_image ? removed_uses + 1 : 0;
}

void mk2_append_budget_relief_report(char *out, size_t outsz, size_t over_bytes)
{
    if (!out || outsz == 0) return;
    char line[256];
    snprintf(line, sizeof line, "Budget overage relief needed: 0x%zX\n", over_bytes);
    stage_append(out, outsz, line);

    std::vector<Mk2ReliefCandidate> cands;
    mk2_collect_budget_relief_candidates(&cands);
    int shown = 0;
    for (size_t i = 0; i < cands.size() && shown < 5; i++) {
        Img *im = &g_img[cands[i].img_i];
        char why[128];
        mk2_relief_candidate_reason(im, &cands[i], why, sizeof why);
        snprintf(line, sizeof line,
                 "Relief candidate: image 0x%02X %s saves 0x%zX (%dx%d, uses %d, %s)\n",
                 im->idx, mk2_relief_action_name(cands[i].kind), cands[i].recover,
                 im->w, im->h, cands[i].uses, why);
        stage_append(out, outsz, line);
        shown++;
    }
    if (shown == 0)
        stage_append(out, outsz, "Relief candidate: none found; try replacing or splitting newly imported art.\n");
}

void draw_mk2_budget_relief_suggestions(const Mk2Budget *budget, int payload_limit)
{
    if (!budget) return;
    int limit = payload_limit < 0 ? 0 : payload_limit;
    bool over = limit > 0 && budget->estimated_payload > (size_t)limit;
    size_t need = over ? (budget->estimated_payload - (size_t)limit) : 0;

    ImGui::Separator();
    ImGui::Text("Budget Relief Suggestions");
    if (over) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Over allocation by 0x%zX (%zu bytes).", need, need);
    } else {
        ImGui::TextDisabled("Use this before packaging to find sprites that can give space back.");
    }
    ImGui::Checkbox("Include placed sprites", &g_budget_relief_show_used);
    ImGui::SameLine();
    ImGui::Checkbox("Include dormant art", &g_budget_relief_show_unused);
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("Rows", &g_budget_relief_rows);
    if (g_budget_relief_rows < 3) g_budget_relief_rows = 3;
    if (g_budget_relief_rows > 32) g_budget_relief_rows = 32;

    std::vector<Mk2ReliefCandidate> cands;
    mk2_collect_budget_relief_candidates(&cands);
    if (cands.empty()) {
        ImGui::TextDisabled("No obvious relief candidates. Try splitting or re-quantizing newly imported source art.");
        return;
    }

    ImGui::TextDisabled("Focus/Select highlights placements. Remove/Delete changes are undoable; Save All still writes backups.");
    if (ImGui::BeginTable("budget_relief_candidates", 8,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 240)))
    {
        ImGui::TableSetupColumn("ii", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("uses", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("bpp", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("save", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("why", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("do", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableHeadersRow();

        bool stop_after_action = false;
        int shown = 0;
        for (size_t ci = 0; ci < cands.size() && shown < g_budget_relief_rows; ci++) {
            Mk2ReliefCandidate c = cands[ci];
            if (c.img_i < 0 || c.img_i >= g_ni) continue;
            Img *im = &g_img[c.img_i];
            shown++;

            char why[160];
            mk2_relief_candidate_reason(im, &c, why, sizeof why);

            ImGui::PushID((int)ci);
            ImGui::TableNextRow();
            if (g_budget_relief_highlight_img_ii == im->idx)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 92, 35, 140));
            if (over && c.recover >= need)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(45, 105, 55, 70));

            ImGui::TableNextColumn();
            char ii_label[24];
            snprintf(ii_label, sizeof ii_label, "0x%02X", im->idx);
            if (ImGui::SmallButton(ii_label))
                mk2_focus_budget_relief_image(c.img_i);
            if (ImGui::IsItemHovered()) {
                g_budget_relief_highlight_img_ii = im->idx;
                ImGui::SetTooltip("Highlight/select this sprite's placements");
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mk2_relief_action_name(c.kind));
            ImGui::TableNextColumn();
            ImGui::Text("%dx%d", im->w, im->h);
            ImGui::TableNextColumn();
            ImGui::Text("%d", c.uses);
            ImGui::TableNextColumn();
            ImGui::Text("%d", c.bpp);
            ImGui::TableNextColumn();
            ImGui::Text("0x%zX", c.recover);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(why);
            ImGui::TableNextColumn();

            if (ImGui::SmallButton("Focus")) {
                mk2_focus_budget_relief_image(c.img_i);
            }
            if (c.uses > 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    int removed = mk2_clear_image_uses(im->idx, "Clear Budget Sprite Uses");
                    char msg[96];
                    snprintf(msg, sizeof msg, "Cleared %d placement(s); art kept", removed);
                    stage_set_toast(msg);
                    stop_after_action = true;
                }
            }

            if (c.kind == MK2_RELIEF_DELETE_UNUSED || c.kind == MK2_RELIEF_REMOVE_SPRITE) {
                ImGui::SameLine();
                if (ImGui::SmallButton(c.uses > 0 ? "Remove" : "Del Art")) {
                    mk2_remove_image_and_uses(c.img_i);
                    stop_after_action = true;
                }
            } else if (c.kind == MK2_RELIEF_TRIM_BORDER) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Trim")) {
                    int saved = trim_image_transparent_border(c.img_i, true);
                    char msg[96];
                    snprintf(msg, sizeof msg, saved > 0 ? "Trimmed %d raw pixel(s)" : "No trim applied", saved);
                    stage_set_toast(msg);
                    stop_after_action = true;
                }
            } else if (c.kind == MK2_RELIEF_COMPRESS_PALETTE) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Compress")) {
                    g_palette_compress_target = c.target_colors > 0 ? c.target_colors : 63;
                    int used = compress_active_image_palette(c.img_i, g_palette_compress_target, true);
                    char msg[128];
                    snprintf(msg, sizeof msg, used >= 0 ? "Compressed %d color(s) to <=%d" : "Compression refused",
                             used, g_palette_compress_target);
                    stage_set_toast(msg);
                    stop_after_action = true;
                }
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Rep")) {
                char path[512] = "";
                if (file_dialog_open("Replace budget sprite from PNG",
                    "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", path, sizeof path))
                {
                    undo_save_ex("Replace Budget Sprite");
                    reimport_image(c.img_i, path);
                    stage_set_toast("Replaced sprite art");
                    stop_after_action = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reimport this image from a smaller/lower-color PNG.");

            ImGui::PopID();
            if (stop_after_action)
                break;
        }
        ImGui::EndTable();
    }
}
