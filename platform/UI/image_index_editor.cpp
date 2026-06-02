#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <stdlib.h>

void draw_img_idx_edit(void)
{
    if (g_img_edit_idx < 0 || g_img_edit_idx >= g_ni) return;
    ImGui::OpenPopup("edit_img_idx");
    ImVec2 mp = ImGui::GetIO().MousePos;
    ImGui::SetNextWindowPos(ImVec2(mp.x, mp.y));
    if (ImGui::BeginPopup("edit_img_idx")) {
        ImGui::Text("New hex index for image %d:", g_img_edit_idx);
        if (ImGui::InputText("##idx", g_img_edit_buf, sizeof g_img_edit_buf,
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            char *end = NULL;
            int val = (int)strtol(g_img_edit_buf, &end, 16);
            if (end && *end == '\0' && val >= 0 && val <= 0xFFFF) {
                int dup = 0;
                for (int i = 0; i < g_ni; i++)
                    if (i != g_img_edit_idx && g_img[i].idx == val) { dup = 1; break; }
                if (!dup) {
                    int before_idx = g_img[g_img_edit_idx].idx;
                    g_img[g_img_edit_idx].idx = val;
                    undo_save_image_index_delta(g_img_edit_idx, before_idx, "Edit Image Index");
                    g_dirty = 1;
                    g_need_rebuild = 1;
                }
                g_img_edit_idx = -1;
            }
        }
        if (ImGui::Button("Cancel")) g_img_edit_idx = -1;
        ImGui::EndPopup();
    } else { g_img_edit_idx = -1; }
}
