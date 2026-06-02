#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

void draw_checkpoints_panel(void)
{
    if (!g_show_checkpoints) return;
    set_left_panel_default(510.0f, 420.0f, 320.0f);
    if (!ImGui::Begin("Checkpoints##cpanel", &g_show_checkpoints)) { ImGui::End(); return; }

    static char checkpoint_label_buf[64] = "";
    ImGui::InputText("Label##cplabel", checkpoint_label_buf, sizeof checkpoint_label_buf);

    int free_slot = -1;
    for (int i = 0; i < CHECKPOINT_N; i++) if (!checkpoint_is_used(i)) { free_slot = i; break; }
    bool can_save = free_slot >= 0;
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::Button("Save Checkpoint")) {
        checkpoint_save(free_slot, checkpoint_label_buf[0] ? checkpoint_label_buf : "Checkpoint");
        checkpoint_label_buf[0] = '\0';
    }
    if (!can_save) ImGui::EndDisabled();
    if (!can_save) { ImGui::SameLine(); ImGui::TextDisabled("(all %d slots used)", CHECKPOINT_N); }

    ImGui::Separator();
    ImGui::Text("Saved checkpoints:");

    for (int i = 0; i < CHECKPOINT_N; i++) {
        if (!checkpoint_is_used(i)) continue;
        ImGui::PushID(i);
        ImGui::Text("[%d] %s", i, checkpoint_get_label(i));
        ImGui::SameLine();
        if (ImGui::SmallButton("Restore")) {
            checkpoint_restore(i);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            checkpoint_delete(i);
        }
        ImGui::PopID();
    }

    int used_count = 0;
    for (int i = 0; i < CHECKPOINT_N; i++) if (checkpoint_is_used(i)) used_count++;
    if (used_count == 0) ImGui::TextDisabled("No checkpoints saved.");

    ImGui::End();
}
