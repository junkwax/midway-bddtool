#include "bg_editor_globals.h"
#include "imgui.h"
#include "undo_manager.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
/* New project dialog state */
int  g_new_template  = 0;
char g_new_name[64]  = "WORLD";
int  g_new_w         = 512;
int  g_new_h         = 256;
int  g_new_depth     = 255;
int  g_new_pals      = 0;

extern const Template g_templates[] = {
    {"Mortal Kombat II",   "MK2STAGE",     1024, 256, 255, 0},
    {"MK2 Walkable Stage",  "MK2WALK",      1200, 256, 255, 0},
    {"MK2 Portal Layered",  "BGPROF",       1203, 254, 255, 0},
    {"MK2 Stock Repair",   "MK2FIX",       1024, 256, 255, 0},
    {"MK2 Floor/Mid/FG",   "MK2LAYR",      1200, 256, 255, 0},
    {"NBA Jam",            "NBAJAMBG",     1024, 240, 255, 0},
    {"NBA Hangtime",       "HANGBG",       1280, 240, 255, 0},
    {"Generic (wide)",     "WORLDWID",     2048, 256, 255, 0},
    {"Generic (square)",   "WORLD",         512, 256, 255, 0},
};
extern const int g_num_templates = sizeof(g_templates) / sizeof(g_templates[0]);

void open_new_mk2_project_from_template(void)
{
    g_new_template = 0;
    snprintf(g_new_name, sizeof g_new_name, "%s", g_templates[0].name);
    g_new_w = g_templates[0].w;
    g_new_h = g_templates[0].h;
    g_new_depth = g_templates[0].depth;
    g_new_pals = g_templates[0].pals;
    request_unsaved_action(UNSAVED_ACTION_SHOW_NEW_PROJECT);
}


void new_project_apply(void)
{
    if (g_new_project_apply_allowed_after_discard) {
        g_new_project_apply_allowed_after_discard = false;
    } else if (!g_unsaved_action_bypass && has_unsaved_work()) {
        request_unsaved_action(UNSAVED_ACTION_APPLY_NEW_PROJECT);
        return;
    }

    if (g_cur_doc < 0 || g_num_docs <= 0)
        doc_add();

    undo_save_ex("New Project");
    editor_project_reset_loaded_stage();
    g_have_bdb = 1;
    char module_line[256];
    snprintf(module_line, sizeof module_line,
             "MOD0 0 %d 0 %d", g_new_w > 0 ? g_new_w - 1 : 0, g_new_h > 0 ? g_new_h - 1 : 0);
    editor_project_set_single_module_line(module_line);

    snprintf(g_name, sizeof g_name, "%s", g_new_name);
    snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d 0",
             g_new_name, g_new_w, g_new_h, g_new_depth, g_bdb_num_modules, g_new_pals);
    snprintf(g_bdb_path, sizeof g_bdb_path, "%s.BDB", g_new_name);
    snprintf(g_bdd_path, sizeof g_bdd_path, "%s.BDD", g_new_name);

    for (int i = 0; i < g_new_pals; i++) {
        char pal_name[64];
        snprintf(pal_name, sizeof pal_name, "PAL_%d", i);
        if (editor_project_append_palette_slot(pal_name, 256, NULL) < 0)
            break;
    }
    g_sel_pal = 0;

    g_need_rebuild = 1;
    g_dirty = 1;
    g_show_new = false;
    if (g_cur_doc >= 0)
        doc_save(g_cur_doc);
}

extern const CardTemplate g_cards[] = {
    { "MK II Stage",        "Standard fighting stage\n1024x256, 6 parallax layers",   0 },
    { "MK II Stage (wide)", "Long panoramic pan\n1200x256, walkable",                 1 },
    { "NBA Jam Court",      "Basketball court\n1024x240 widescreen",                  5 },
    { "Blank Canvas",       "Empty world\nSet your own dimensions",                             8 },
};
bool g_card_blank_open = false;


void draw_new_project(void)
{
    if (!g_show_new) return;
    ImGui::OpenPopup("New Project");

    if (g_simple_mode)
        ImGui::SetNextWindowSize(ImVec2(460, 220), ImGuiCond_Always);
    else
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("New Project", &g_show_new,
                               g_simple_mode ? 0 : ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (g_simple_mode) {
            /* ---- template cards ---- */
            ImGui::TextUnformatted("Choose a starting point:");
            ImGui::Spacing();
            float card_w = 100.0f, card_h = 72.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            for (int c = 0; c < 4; c++) {
                if (c > 0) ImGui::SameLine(0, 8);
                ImGui::PushID(c);
                bool blank = (c == 3);
                bool active = blank && g_card_blank_open;
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f,0.45f,0.65f,1));
                if (ImGui::Button("##card", ImVec2(card_w, card_h))) {
                    if (blank) {
                        g_card_blank_open = !g_card_blank_open;
                    } else {
                        int ti = g_cards[c].ti;
                        g_new_template = ti;
                        snprintf(g_new_name, sizeof g_new_name, "%s", g_templates[ti].name);
                        g_new_w = g_templates[ti].w; g_new_h = g_templates[ti].h;
                        g_new_depth = g_templates[ti].depth; g_new_pals = g_templates[ti].pals;
                        new_project_apply();
                        g_show_new = false;
                    }
                }
                if (active) ImGui::PopStyleColor();
                /* overlay title + desc on the button */
                ImVec2 bmin = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(bmin.x + 5, bmin.y + 5),
                    IM_COL32(220,220,255,255), g_cards[c].title);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(bmin.x + 5, bmin.y + 22),
                    IM_COL32(160,160,200,200), g_cards[c].desc);
                ImGui::PopID();
            }
            ImGui::PopStyleVar();

            if (g_card_blank_open) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::SetNextItemWidth(140);
                ImGui::InputInt("Width",  &g_new_w); ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                ImGui::InputInt("Height", &g_new_h);
                if (g_new_w < 1) g_new_w = 1;
                if (g_new_h < 1) g_new_h = 1;
                ImGui::Spacing();
                if (ImGui::Button("Create", ImVec2(100, 0))) {
                    g_new_depth = 255; g_new_pals = 0;
                    snprintf(g_new_name, sizeof g_new_name, "WORLD");
                    new_project_apply();
                    g_show_new = false; g_card_blank_open = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                    g_new_project_apply_allowed_after_discard = false;
                    g_show_new = false; g_card_blank_open = false;
                }
            } else {
                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                    g_new_project_apply_allowed_after_discard = false;
                    g_show_new = false;
                }
            }
        } else {
            /* ---- Advanced: existing form ---- */
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::BeginCombo("Template", g_templates[g_new_template].label)) {
                for (int i = 0; i < g_num_templates; i++) {
                    if (ImGui::Selectable(g_templates[i].label, i == g_new_template)) {
                        g_new_template = i;
                        snprintf(g_new_name, sizeof g_new_name, "%s", g_templates[i].name);
                        g_new_w     = g_templates[i].w;
                        g_new_h     = g_templates[i].h;
                        g_new_depth = g_templates[i].depth;
                        g_new_pals  = g_templates[i].pals;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::InputText("World Name", g_new_name, sizeof g_new_name);
            ImGui::InputInt("Width",  &g_new_w);
            ImGui::InputInt("Height", &g_new_h);
            ImGui::InputInt("Depth",  &g_new_depth);
            ImGui::InputInt("Palettes", &g_new_pals);
            if (g_new_w < 1) g_new_w = 1;
            if (g_new_h < 1) g_new_h = 1;
            if (g_new_depth < 1) g_new_depth = 1;
            if (g_new_pals < 0) g_new_pals = 0;
            ImGui::Separator();
            if (ImGui::Button("Create", ImVec2(80, 0))) new_project_apply();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                g_new_project_apply_allowed_after_discard = false;
                g_show_new = false;
            }
        }
        ImGui::EndPopup();
    }
}

