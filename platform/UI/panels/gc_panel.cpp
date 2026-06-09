#include "UI/panels/UtilityPanels.h"
#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

void GarbageCollectPanel::render()
{
    if (!g_show_gc) return;
    set_left_panel_default(92.0f, 520.0f, 400.0f);
    if (!ImGui::Begin("Garbage Collect##gcpanel", &g_show_gc)) { ImGui::End(); return; }

    int image_cap = editor_project_image_capacity();
    static std::vector<unsigned char> gc_sel;
    if (image_cap <= 0) { ImGui::End(); return; }
    if ((int)gc_sel.size() != image_cap)
        gc_sel.assign((size_t)image_cap, 0);
    int unused_count = 0;
    int unused_pixels = 0;
    for (int i = 0; i < g_ni; i++) {
        if (image_use_count(g_img[i].idx) == 0) {
            unused_count++;
            unused_pixels += g_img[i].w * g_img[i].h;
        }
    }

    ImGui::Text("Unused images: %d  (%d raw pixels)", unused_count, unused_pixels);
    if (unused_count == 0) {
        ImGui::TextDisabled("No orphaned images.");
        ImGui::End(); return;
    }

    if (ImGui::SmallButton("Select All"))
        for (int i = 0; i < g_ni; i++)
            gc_sel[i] = image_use_count(g_img[i].idx) == 0 ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("Select Imported"))
        for (int i = 0; i < g_ni; i++)
            gc_sel[i] = (image_use_count(g_img[i].idx) == 0 && image_is_imported_asset(&g_img[i])) ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("Deselect All"))
        std::fill(gc_sel.begin(), gc_sel.end(), 0);
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete Imported Unused")) {
        delete_unused_images_impl(true, "Delete Imported Unused Images");
        std::fill(gc_sel.begin(), gc_sel.end(), 0);
    }

    ImGui::Separator();

    int sel_count = 0, sel_pixels = 0;
    if (ImGui::BeginTable("gc_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, -40))) {
        ImGui::TableSetupColumn("Del",    ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("idx",    ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("size",   ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("pixels", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < g_ni; i++) {
            if (image_use_count(g_img[i].idx) != 0) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char cid[16]; snprintf(cid, sizeof cid, "##gc%d", i);
            bool checked = gc_sel[i] != 0;
            if (ImGui::Checkbox(cid, &checked))
                gc_sel[i] = checked ? 1 : 0;
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("0x%02X", g_img[i].idx);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%dx%d", g_img[i].w, g_img[i].h);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", g_img[i].w * g_img[i].h);
            if (gc_sel[i]) { sel_count++; sel_pixels += g_img[i].w * g_img[i].h; }
        }
        ImGui::EndTable();
    }

    ImGui::Text("%d selected (%d px)", sel_count, sel_pixels);
    ImGui::SameLine();
    bool can_del = sel_count > 0;
    if (!can_del) ImGui::BeginDisabled();
    if (ImGui::Button("Delete Selected")) {
        undo_save_ex("GC Delete");
        for (int i = g_ni - 1; i >= 0; i--) {
            if (image_use_count(g_img[i].idx) == 0 && gc_sel[i]) {
                editor_project_delete_image_slot(i);
            }
        }
        std::fill(gc_sel.begin(), gc_sel.end(), 0);
        g_need_rebuild = 1;
        g_dirty = 1;
        stage_set_toast("Garbage collected");
    }
    if (!can_del) ImGui::EndDisabled();

    ImGui::End();
}
