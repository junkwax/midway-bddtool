#include "bg_editor_globals.h"
#include "imgui.h"

#include <cstdio>

bool has_unsaved_work(void)
{
    if (g_dirty) return true;
    for (int i = 0; i < g_num_docs && i < MAX_DOCS; i++) {
        if (g_docs[i].loaded && g_docs[i].dirty)
            return true;
    }
    return false;
}

void clear_unsaved_action(void)
{
    g_unsaved_action = UNSAVED_ACTION_NONE;
    g_unsaved_doc_idx = -1;
    g_unsaved_action_path[0] = '\0';
}

void execute_unsaved_action(int action)
{
    g_unsaved_action_bypass = true;
    switch (action) {
    case UNSAVED_ACTION_CLOSE_APP:
        g_close_approved = true;
        break;
    case UNSAVED_ACTION_OPEN_STAGE:
        open_stage_path_now(g_unsaved_action_path);
        break;
    case UNSAVED_ACTION_SHOW_NEW_PROJECT:
        if (g_unsaved_continue_without_save)
            g_new_project_apply_allowed_after_discard = true;
        g_show_new = true;
        break;
    case UNSAVED_ACTION_APPLY_NEW_PROJECT:
        new_project_apply();
        break;
    case UNSAVED_ACTION_NEW_SIMPLE_MK2:
        open_mk2_simple_level_dialog();
        break;
    case UNSAVED_ACTION_NEW_BG_PROOF:
        create_bg_proof_level();
        break;
    case UNSAVED_ACTION_NEW_CHECKER:
        create_checker_test_level();
        break;
    case UNSAVED_ACTION_CLOSE_DOC:
        if (g_unsaved_doc_idx >= 0 && g_unsaved_doc_idx < g_num_docs)
            doc_close(g_unsaved_doc_idx);
        break;
    default:
        break;
    }
    g_unsaved_action_bypass = false;
    g_unsaved_continue_without_save = false;
    clear_unsaved_action();
}

void request_unsaved_action(int action, const char *path, int doc_idx)
{
    if (g_unsaved_action_bypass || !has_unsaved_work()) {
        if (path && path[0])
            snprintf(g_unsaved_action_path, sizeof g_unsaved_action_path, "%s", path);
        g_unsaved_doc_idx = doc_idx;
        execute_unsaved_action(action);
        return;
    }

    g_unsaved_action = action;
    g_unsaved_doc_idx = doc_idx;
    snprintf(g_unsaved_action_path, sizeof g_unsaved_action_path, "%s", path ? path : "");
    g_unsaved_prompt_open = true;
}


void draw_unsaved_action_prompt(void)
{
    if (g_unsaved_prompt_open) {
        ImGui::OpenPopup("Unsaved Changes");
        g_unsaved_prompt_open = false;
    }

    bool open = true;
    if (!ImGui::BeginPopupModal("Unsaved Changes", &open, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("You have unsaved changes.");
    ImGui::TextWrapped("Backups are still created when files are saved, but they do not replace saving your current work.");
    ImGui::Separator();

    if (ImGui::Button("Save and Continue", ImVec2(150, 0))) {
        if (save_all_dirty_documents()) {
            int action = g_unsaved_action;
            ImGui::CloseCurrentPopup();
            execute_unsaved_action(action);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
        int action = g_unsaved_action;
        g_unsaved_continue_without_save = true;
        ImGui::CloseCurrentPopup();
        execute_unsaved_action(action);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0))) {
        clear_unsaved_action();
        ImGui::CloseCurrentPopup();
    }
    if (!open)
        clear_unsaved_action();
    ImGui::EndPopup();
}

