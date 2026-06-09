#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"
#include "undo_manager.h"
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "Core/bdd_format.h"

struct PaletteSlotUndoCapture {
    int active;
    int pal_i;
    int color_i;
    int count;
    char name[64];
    Uint32 colors[256];
    Uint16 rgb555[256];
    int rgb555_valid;
    int rgb555_count;
};

static PaletteSlotUndoCapture g_palette_name_undo = {};
static PaletteSlotUndoCapture g_palette_color_undo = {};

static void palette_slot_undo_capture(PaletteSlotUndoCapture *cap, int pal_i, int color_i)
{
    if (!cap) return;
    memset(cap, 0, sizeof(*cap));
    if (pal_i < 0 || pal_i >= g_n_pals) return;
    cap->active = 1;
    cap->pal_i = pal_i;
    cap->color_i = color_i;
    cap->count = g_pal_count[pal_i];
    snprintf(cap->name, sizeof cap->name, "%s", g_pal_name[pal_i]);
    memcpy(cap->colors, g_pals[pal_i], sizeof cap->colors);
    cap->rgb555_valid = editor_project_get_palette_rgb555_cache(pal_i, cap->rgb555, 256);
    cap->rgb555_count = cap->rgb555_valid ? g_pal_count[pal_i] : 0;
}

static int palette_slot_undo_commit(PaletteSlotUndoCapture *cap, const char *label)
{
    int saved;
    if (!cap || !cap->active) return 0;
    saved = undo_save_palette_slot_delta(cap->pal_i, cap->colors, cap->count,
                                         cap->name, cap->rgb555,
                                         cap->rgb555_valid, cap->rgb555_count,
                                         label);
    memset(cap, 0, sizeof(*cap));
    return saved;
}

static void palette_usage_stats(int pal_i, int *out_images, int *out_objects)
{
    int images = 0;
    int objects = 0;
    for (int ii = 0; ii < g_ni; ii++) {
        if (g_img[ii].pal_idx == pal_i)
            images++;
    }
    for (int oi = 0; oi < g_no; oi++) {
        Img *im = img_find(g_obj[oi].ii);
        if (!im) continue;
        if (object_palette_for_image(&g_obj[oi], im) == pal_i)
            objects++;
    }
    if (out_images) *out_images = images;
    if (out_objects) *out_objects = objects;
}

static void draw_palette_slot_table(int selected_object_palette)
{
    float table_h = 24.0f + (float)g_n_pals * ImGui::GetTextLineHeightWithSpacing();
    if (table_h < 70.0f) table_h = 70.0f;
    if (table_h > 140.0f) table_h = 140.0f;

    ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("palette_slot_table", 4, flags, ImVec2(0, table_h))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Clr", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < g_n_pals; i++) {
            int image_refs = 0, object_refs = 0;
            palette_usage_stats(i, &image_refs, &object_refs);
            ImGui::TableNextRow();
            if (i == g_sel_pal)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 105, 150, 75));
            else if (i == selected_object_palette)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(35, 125, 65, 60));

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i);
            ImGui::TableSetColumnIndex(1);
            char label[96];
            snprintf(label, sizeof label, "%s%s", g_pal_name[i],
                     i == selected_object_palette ? "  *" : "");
            if (ImGui::Selectable(label, i == g_sel_pal, ImGuiSelectableFlags_SpanAllColumns))
                g_sel_pal = i;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%d image slot(s), %d object placement(s)", image_refs, object_refs);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", g_pal_count[i]);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d/%d", image_refs, object_refs);
        }
        ImGui::EndTable();
    }
}

void draw_selected_palette_swatches(int pal_idx)
{
    if (pal_idx < 0 || pal_idx >= g_n_pals) return;

    if (g_palette_color_undo.active && g_palette_color_undo.pal_i != pal_idx)
        palette_slot_undo_commit(&g_palette_color_undo, "Edit Palette Color");

    float win_w = ImGui::GetContentRegionAvail().x;
    int cols = (int)(win_w / 22.0f);
    if (cols < 1) cols = 1;
    ImVec2 swatch(18.0f, 18.0f);
    int pc = g_pal_count[pal_idx];
    if (pc < 0) pc = 0;
    if (pc > 256) pc = 256;

    for (int i = 0; i < pc; i++) {
        Uint32 c = g_pals[pal_idx][i];
        ImVec4 col4(
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 8)  & 0xFF) / 255.0f,
            (c         & 0xFF) / 255.0f,
            ((c >> 24) & 0xFF) / 255.0f);
        ImGui::PushID(i);
        if (i % cols != 0) ImGui::SameLine();

        if (ImGui::ColorButton("##sw", col4,
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, swatch))
        {
            palette_slot_undo_capture(&g_palette_color_undo, pal_idx, i);
            ImGui::OpenPopup("col_edit");
        }

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%d: #%02X%02X%02X", i,
                        (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            ImGui::EndTooltip();
        }

        if (ImGui::BeginPopup("col_edit")) {
            static float col[3] = {1,1,1};
            col[0] = ((c >> 16) & 0xFF) / 255.0f;
            col[1] = ((c >> 8)  & 0xFF) / 255.0f;
            col[2] = (c         & 0xFF) / 255.0f;
            if (ImGui::ColorPicker3("RGB", col)) {
                Uint8 r = (Uint8)(col[0] * 255.0f);
                Uint8 g_ = (Uint8)(col[1] * 255.0f);
                Uint8 b = (Uint8)(col[2] * 255.0f);
                Uint32 next = 0xFF000000u | ((Uint32)r << 16)
                                         | ((Uint32)g_ << 8)
                                         | b;
                if (editor_project_set_palette_color(pal_idx, i, next))
                    g_dirty = 1;
            }
            ImGui::EndPopup();
        }
        if (g_palette_color_undo.active &&
            g_palette_color_undo.pal_i == pal_idx &&
            g_palette_color_undo.color_i == i &&
            !ImGui::IsPopupOpen("col_edit"))
            palette_slot_undo_commit(&g_palette_color_undo, "Edit Palette Color");
        ImGui::PopID();
    }

    if (g_palette_color_undo.active &&
        g_palette_color_undo.pal_i == pal_idx &&
        g_palette_color_undo.color_i >= pc)
        palette_slot_undo_commit(&g_palette_color_undo, "Edit Palette Color");
}

void draw_palette(void)
{
    right_panel_set_next(RIGHT_PANEL_PALETTES);
    bool open = ImGui::Begin("Palettes", NULL);
    right_panel_after_begin(RIGHT_PANEL_PALETTES);
    if (!open) {
        ImGui::End();
        return;
    }

    if (g_n_pals == 0) {
        ImGui::TextUnformatted("No palettes loaded.");
        ImGui::End();
        return;
    }

    int sel_obj_pal = -1;
    static int s_last_follow_obj = -2;
    static int s_last_follow_pal = -2;
    if (g_hl_obj >= 0 && g_hl_obj < g_no) {
        sel_obj_pal = g_obj[g_hl_obj].fl;
        if (sel_obj_pal >= 0 && sel_obj_pal < g_n_pals &&
            (g_hl_obj != s_last_follow_obj || sel_obj_pal != s_last_follow_pal)) {
            g_sel_pal = sel_obj_pal;
            s_last_follow_obj = g_hl_obj;
            s_last_follow_pal = sel_obj_pal;
        }
    } else {
        s_last_follow_obj = -2;
        s_last_follow_pal = -2;
    }

    if (g_sel_pal < 0) g_sel_pal = 0;
    if (g_sel_pal >= g_n_pals) g_sel_pal = g_n_pals - 1;

    /* show which palette the selected object uses */
    if (g_hl_obj >= 0 && g_hl_obj < g_no) {
        if (sel_obj_pal >= 0 && sel_obj_pal < g_n_pals) {
            ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "Object %d -> pal %d: %s",
                              g_hl_obj, sel_obj_pal, g_pal_name[sel_obj_pal]);
        }
    }

    draw_palette_slot_table(sel_obj_pal);

    int pc = g_pal_count[g_sel_pal];
    if (pc < 0) pc = 0;
    if (pc > 256) pc = 256;
    char pal_name[64];
    snprintf(pal_name, sizeof pal_name, "%s", g_pal_name[g_sel_pal]);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 62.0f);
    ImGui::InputText("##palname", pal_name, sizeof pal_name);
    if (ImGui::IsItemActivated())
        palette_slot_undo_capture(&g_palette_name_undo, g_sel_pal, -1);
    if (ImGui::IsItemEdited() &&
        editor_project_set_palette_slot(g_sel_pal, pal_name, g_pal_count[g_sel_pal], NULL))
        g_dirty = 1;
    if (ImGui::IsItemDeactivated())
        palette_slot_undo_commit(&g_palette_name_undo, "Rename Palette");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rename palette");
    ImGui::SameLine();
    ImGui::TextDisabled("%d clr", pc);

    ImGui::BeginChild("selected_palette_swatch_scroller", ImVec2(0, 88.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    draw_selected_palette_swatches(g_sel_pal);
    ImGui::EndChild();

    if (ImGui::Button("+")) {
        undo_save();
        char name[64];
        snprintf(name, sizeof name, "PAL_%d", g_n_pals);
        int dst = editor_project_append_palette_slot(name, 256, NULL);
        if (dst >= 0) g_sel_pal = dst;
    }
    ImGui::SameLine();
    if (ImGui::Button("-") && g_n_pals > 0) {
        undo_save();
        editor_project_delete_palette_slot(g_sel_pal);
        if (g_sel_pal >= g_n_pals) g_sel_pal = g_n_pals - 1;
    }

    /* count duplicates for the button label */
    {
        int dup_count = 0;
        for (int i = 0; i < g_n_pals; i++)
            for (int j = i + 1; j < g_n_pals; j++)
                if (memcmp(g_pals[i], g_pals[j], 256 * sizeof(Uint32)) == 0)
                    dup_count++;
        char merge_lbl[32];
        snprintf(merge_lbl, sizeof merge_lbl, "Merge (%d dup)", dup_count);
        bool has_dups = dup_count > 0;
        if (!has_dups) ImGui::BeginDisabled();
        if (ImGui::Button(merge_lbl)) merge_duplicate_palettes();
        if (!has_dups) ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Merge byte-identical palettes and remap all image/object references.\nThis frees palette slots and reduces ROM CLUT usage.");
        ImGui::SameLine();
        if (ImGui::Button("Rebuild All")) batch_palette_rebuild();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remap every palette to contiguous 1-N indices (union of all images on that palette).\nReduces wasted palette entries across all shared palettes.");
        ImGui::SameLine();
        if (ImGui::Button("Delete Unused")) {
            int removed = remove_unused_palettes_impl(true);
            char msg[96];
            snprintf(msg, sizeof msg, removed ? "Deleted %d unused palette(s)" : "No unused palettes", removed);
            stage_set_toast(msg);
        }
    }

    if (ImGui::CollapsingHeader("Blend / Merge Palettes"))
        draw_palette_blend_merge_tool();

    if (ImGui::CollapsingHeader("Smart Palette Grouper"))
        draw_mk2_smart_palette_grouper();

    ImGui::End();
}
