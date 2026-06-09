#include "Core/editor_app_globals.h"
#include "UI/tools/mk2_stage_config.h"
#include "UI/view/toast_notifications.h"

#include <imgui.h>

void stage_copy_command(const char *action)
{
    if (!stage_write_config()) return;
    stage_build_command(action, g_stage_last_command, sizeof g_stage_last_command);
    ImGui::SetClipboardText(g_stage_last_command);
    stage_set_toast("Copied Stage Kit command");
}
