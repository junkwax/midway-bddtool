#include "Core/editor_app_globals.h"
#include "UI/dialogs/save_error_popup.h"
#include "imgui.h"

#include <stdio.h>
#include <string.h>

static bool g_save_error_popup_open = false;
static char g_save_error_popup_msg[1024] = "";

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
    return "The detailed save error is in save_errors.log.";
}

void open_save_error_popup(const char *detail)
{
    const char *safe_detail = (detail && detail[0]) ? detail : "Unknown save failure.";
    snprintf(g_save_error_popup_msg, sizeof g_save_error_popup_msg,
             "Save failed.\n\n%s\n\n%s",
             safe_detail, save_error_hint(safe_detail));
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
        ImGui::Separator();
        ImGui::TextDisabled("See save_errors.log for the full save history.");
        if (ImGui::Button("OK", ImVec2(96, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!g_show_new)
        g_new_project_apply_allowed_after_discard = false;
}
