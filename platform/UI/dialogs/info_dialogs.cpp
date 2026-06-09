#include "Core/app_version.h"
#include "Core/editor_app_globals.h"
#include "UI/dialogs/info_dialogs.h"
#include "UI/view/right_panel_layout.h"
#include "imgui.h"

void draw_help(void)
{
    if (!g_show_help) return;
    set_left_panel_default(92.0f, 480.0f, 460.0f);
    if (!ImGui::Begin("Keyboard Shortcuts", &g_show_help))
        return;

    auto r = [](const char *k, const char *a) {
        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(k);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(a);
    };
    auto hdr = [](const char *title) {
        ImGui::TableNextRow();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40,50,70,200));
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f,0.85f,1.0f,1.0f), "%s", title);
        ImGui::TableNextColumn(); ImGui::TextUnformatted("");
    };

    int flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("help", 2, flags)) {
        float sc = ImGui::GetFontSize() / 13.0f;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Key",    ImGuiTableColumnFlags_WidthFixed, 140 * sc);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        hdr("Navigation");
        r("Arrow keys",       "Scroll world  (with selection: nudge)");
        r("Shift+Arrow",      "Nudge selected by grid step");
        r("Scroll wheel",     "Zoom in/out");
        r("Ctrl+Home",        "Fit whole stage in view");
        r("Ctrl+1",           "Zoom 1:1 pixels");
        r("Ctrl+Shift+Z",     "Zoom to selection");
        r("Home",             "Reset view");

        hdr("Edit");
        r("Ctrl+Z",           "Undo (32 levels)");
        r("Ctrl+Y",           "Redo");
        r("Ctrl+C",           "Copy selected");
        r("Ctrl+V",           "Paste");
        r("Ctrl+X",           "Cut");
        r("Ctrl+D",           "Duplicate in place");
        r("D",                "Duplicate (quick)");
        r("Del / Backspace",  "Delete selected");
        r("LMB drag obj",     "Move objects");
        r("Alt+LMB drag",     "Clone + move");

        hdr("Selection");
        r("Click obj",        "Select single");
        r("Drag empty area",  "Marquee select/highlight section");
        r("Ctrl+click",       "Toggle in selection");
        r("Shift+click",      "Range select in list");
        r("Ctrl+A",           "Select all  (again = deselect all)");
        r("Ctrl+I",           "Invert selection");
        r("Escape",           "Deselect all");
        r("X",                "H-flip selected");
        r("Y",                "V-flip selected");

        hdr("View");
        r("Shift+T",          "Toggle grid");
        r("Shift+B",          "Toggle borders");
        r("O",                "Toggle object info overlay");
        r("Shift+O",          "Toggle object sprites");
        r("View > Layer Colors", "Toggle layer color tint");
        r("F11 / Space",      "Preview mode");

        hdr("File");
        r("Ctrl+S",           g_simple_mode ? "Save" : "Save BDB + BDD");
        r("Ctrl+O",           "Open file");

        if (!g_simple_mode) {
            hdr("Advanced");
            r("Tab",              "Object picker combo");
            r("F1",               "This help");
        }

        ImGui::EndTable();
    }
    ImGui::End();
}

void draw_about(void)
{
    if (!g_about_open) return;
    ImGui::OpenPopup("About");
    if (ImGui::BeginPopupModal("About", &g_about_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("midway-bddtool");
        ImGui::Text("Version %s", BDDVIEW_APP_VERSION);
        ImGui::Text("Background viewer/editor for Midway arcade games");
        ImGui::Separator();
        ImGui::Text("BDB/BDD format (Mortal Kombat, MK II, etc.)");
        ImGui::Text("Built with SDL2 + Dear ImGui");
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(80, 0)))
            g_about_open = false;
        ImGui::EndPopup();
    }
}
