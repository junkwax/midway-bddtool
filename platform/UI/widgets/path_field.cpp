#include "UI/dialogs/native_file_dialogs.h"
#include "UI/widgets/path_field.h"

#include "imgui.h"

#include <cstdio>
void draw_path_field(const char *label, char *buf, size_t bufsz,
                     const char *dialog_title, const char *filter)
{
    ImGui::InputText(label, buf, bufsz);
    ImGui::SameLine();
    char button[64];
    snprintf(button, sizeof button, "...##%s", label);
    if (ImGui::SmallButton(button)) {
        char path[512] = "";
        if (file_dialog_open(dialog_title, filter, path, sizeof path))
            snprintf(buf, bufsz, "%s", path);
    }
}

