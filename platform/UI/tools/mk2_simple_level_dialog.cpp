#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>
#include <string.h>

static bool g_show_mk2_simple_level = false;
static char g_mk2_simple_bg[512] = "";
static char g_mk2_simple_floor[512] = "";
static char g_mk2_simple_corner[512] = "";
static char g_mk2_simple_front[512] = "";

static const char *simple_path_basename(const char *path)
{
    if (!path) return "";
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *p = a > b ? a : b;
    return p ? p + 1 : path;
}

static bool simple_file_exists(const char *path)
{
    if (!path || !path[0]) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static void mk2_simple_pick_image(const char *label, char *buf, size_t bufsz)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(130.0f);
    const char *shown = buf[0] ? simple_path_basename(buf) : "(none)";
    ImGui::TextColored(buf[0] ? ImVec4(0.78f, 0.88f, 1.0f, 1.0f)
                              : ImVec4(1.0f, 0.62f, 0.25f, 1.0f),
                       "%s", shown);
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("Choose")) {
        char path[512] = "";
        if (file_dialog_open(label, "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", path, sizeof path))
            snprintf(buf, bufsz, "%s", path);
    }
    ImGui::PopID();
}

void open_mk2_simple_level_dialog(void)
{
    g_simple_mode = true;
    g_show_mk2_workflow = false;
    g_show_mk2_stage_kit = false;
    g_show_images = false;
    g_show_layers = false;
    g_show_mk2_simple_level = true;
    settings_save();
}

void draw_mk2_simple_level_dialog(void)
{
    if (!g_show_mk2_simple_level) return;
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    ImGui::OpenPopup("Simple MK2 Level");
    if (!ImGui::BeginPopupModal("Simple MK2 Level", &g_show_mk2_simple_level,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Pick four PNGs");
    ImGui::Separator();
    mk2_simple_pick_image("Background", g_mk2_simple_bg, sizeof g_mk2_simple_bg);
    mk2_simple_pick_image("Floor", g_mk2_simple_floor, sizeof g_mk2_simple_floor);
    mk2_simple_pick_image("Corner", g_mk2_simple_corner, sizeof g_mk2_simple_corner);
    mk2_simple_pick_image("Front Sprite", g_mk2_simple_front, sizeof g_mk2_simple_front);

    bool ready = simple_file_exists(g_mk2_simple_bg) &&
                 simple_file_exists(g_mk2_simple_floor) &&
                 simple_file_exists(g_mk2_simple_corner) &&
                 simple_file_exists(g_mk2_simple_front);
    ImGui::Separator();
    if (!ready) ImGui::BeginDisabled();
    if (ImGui::Button("Create MK2 Level", ImVec2(-1, 0))) {
        if (create_mk2_simple_four_image_level(g_mk2_simple_bg,
                                               g_mk2_simple_floor,
                                               g_mk2_simple_corner,
                                               g_mk2_simple_front)) {
            g_show_mk2_simple_level = false;
            ImGui::CloseCurrentPopup();
        }
    }
    if (!ready) ImGui::EndDisabled();
    if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
        g_show_mk2_simple_level = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
