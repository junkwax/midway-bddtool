#include "Core/editor_app_globals.h"
#include "UI/dialogs/save_error_popup.h"
#include "UI/tools/mk2_runtime_actor_tool.h"
#include "UI/view/toast_notifications.h"
#include "imgui.h"

#include <stdio.h>
#include <string.h>

static bool g_save_error_popup_open = false;
static bool g_save_error_runtime_preview_block = false;
static char g_save_error_popup_msg[1024] = "";

bool save_all_project(void);

static const char *save_error_hint(const char *detail)
{
    if (!detail || !detail[0])
        return "Check that the target folder is writable and that the files still exist.";
    if (strstr(detail, "read-only"))
        return "The target file is read-only. Clear the read-only flag or save to a writable copy.";
    if (strstr(detail, "winerr=5"))
        return "Windows reported access denied. The file may be read-only, locked by another process, or outside your write permissions.";
    if (strstr(detail, "cannot open temp"))
        return "The editor could not create the temporary save file. Check folder permissions and available disk space.";
    if (strstr(detail, "backup failed"))
        return "The editor could not make its safety backup. Check folder permissions before retrying.";
    if (strstr(detail, "runtime preview sprite"))
        return "Use the buttons below: save the runtime sidecar if you changed actors, then discard the preview sprites and retry the save.";
    return "The detailed save error is in save_errors.log.";
}

void open_save_error_popup(const char *detail)
{
    const char *safe_detail = (detail && detail[0]) ? detail : "Unknown save failure.";
    snprintf(g_save_error_popup_msg, sizeof g_save_error_popup_msg,
             "Save failed.\n\n%s\n\n%s",
             safe_detail, save_error_hint(safe_detail));
    g_save_error_runtime_preview_block =
        strstr(safe_detail, "runtime preview sprite") != NULL;
    g_save_error_popup_open = true;
}

void draw_save_error_popup(void)
{
    if (g_save_error_popup_open) {
        ImGui::OpenPopup("Save Failed");
        g_save_error_popup_open = false;
    }
    bool open = true;
    if (ImGui::BeginPopupModal("Save Failed", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", g_save_error_popup_msg);
        if (g_save_error_runtime_preview_block) {
            ImGui::Separator();
            if (ImGui::Button("Save Runtime Sidecar", ImVec2(220, 0))) {
                stage_set_toast(runtime_actor_sidecar_save()
                                ? "Runtime sidecar saved"
                                : "Runtime sidecar save failed");
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard Previews + Save", ImVec2(220, 0))) {
                runtime_actor_discard_preview_imports();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                /* Retry now that the block is cleared; a new failure reopens
                   this popup with the fresh error text. */
                save_all_project();
                return;
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("See save_errors.log for the full save history.");
        if (ImGui::Button("OK", ImVec2(96, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!g_show_new)
        g_new_project_apply_allowed_after_discard = false;
}
