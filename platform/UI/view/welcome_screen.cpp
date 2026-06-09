#include "bg_editor_globals.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
/* open a stage from a full path (shared by welcome open-file and recent rows) */
static void welcome_open_path(const char *path)
{
    request_unsaved_action(UNSAVED_ACTION_OPEN_STAGE, path);
}

/* return a short relative-time label for a file's mtime (e.g. "2 days ago") */
static void welcome_rel_time(const char *path, char *buf, int bufsz)
{
    struct stat st;
    if (stat(path, &st) != 0) { snprintf(buf, bufsz, ""); return; }
    time_t now = time(NULL);
    long diff = (long)(now - st.st_mtime);
    if (diff < 0) diff = 0;
    if      (diff < 120)         snprintf(buf, bufsz, "just now");
    else if (diff < 3600)        snprintf(buf, bufsz, "%ld min ago",  diff/60);
    else if (diff < 7200)        snprintf(buf, bufsz, "1 hour ago");
    else if (diff < 86400)       snprintf(buf, bufsz, "%ld hours ago", diff/3600);
    else if (diff < 172800)      snprintf(buf, bufsz, "yesterday");
    else if (diff < 30*86400)    snprintf(buf, bufsz, "%ld days ago",  diff/86400);
    else if (diff < 365*86400)   snprintf(buf, bufsz, "%ld weeks ago", diff/604800);
    else                         snprintf(buf, bufsz, "%ld months ago",diff/2592000);
}

bool welcome_visible(void)
{
    return g_ni <= 0 && !g_have_bdb && !g_preview_mode && g_welcome_show && !g_show_new;
}

void draw_welcome(void)
{
    if (!welcome_visible()) return;
    ImVec2 c = ImGui::GetIO().DisplaySize;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.14f, 0.20f, 0.96f));

    if (g_simple_mode) {
        /* ---- Simple mode: two-column welcome ---- */
        const float W = 640.0f, H = 320.0f;
        ImGui::SetNextWindowPos(ImVec2(c.x/2 - W/2, c.y/2 - H/2));
        ImGui::SetNextWindowSize(ImVec2(W, H));
        ImGui::SetNextWindowFocus();
        ImGui::Begin("##welcome", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        /* title */
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "midway-bddtool");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("Background Editor for Midway Arcade Games");
        ImGui::Separator();
        ImGui::Spacing();

        float col_left  = W * 0.58f - ImGui::GetStyle().ItemSpacing.x;
        float col_right = W * 0.40f;

        /* --- left column: recent stages --- */
        ImGui::BeginChild("##wleft", ImVec2(col_left, H - 110.0f), false);
        ImGui::TextColored(ImVec4(0.7f,0.9f,1.0f,1.0f), "Recent Stages");
        ImGui::Spacing();
        if (g_recent_count == 0) {
            ImGui::TextDisabled("No recent stages.");
        } else {
            int shown = g_recent_count < 5 ? g_recent_count : 5;
            for (int i = 0; i < shown; i++) {
                /* extract basename without extension */
                const char *p = g_recent_files[i];
                const char *sl = strrchr(p, '/');
                const char *bs = strrchr(p, '\\');
                if (bs && bs > sl) sl = bs;
                const char *base = sl ? sl + 1 : p;
                char name[64]; snprintf(name, sizeof name, "%s", base);
                /* strip extension */
                char *dot = strrchr(name, '.'); if (dot) *dot = '\0';

                char age[32]; welcome_rel_time(p, age, sizeof age);
                char label[128];
                if (age[0]) snprintf(label, sizeof label, "%s##rec%d", name, i);
                else        snprintf(label, sizeof label, "%s##rec%d", name, i);

                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f,0.22f,0.30f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f,0.32f,0.48f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.35f,0.42f,0.60f,1.0f));
                if (ImGui::Button(label, ImVec2(col_left - 4, 30))) {
                    welcome_open_path(p);
                }
                ImGui::PopStyleColor(3);
                /* age label to the right of the button */
                if (age[0]) {
                    ImVec2 bmax = ImGui::GetItemRectMax();
                    ImVec2 bmin = ImGui::GetItemRectMin();
                    float tw = ImGui::CalcTextSize(age).x;
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(bmax.x - tw - 6, bmin.y + 8),
                        IM_COL32(130,130,160,200), age);
                }
            }
        }
        ImGui::Spacing();
        if (ImGui::SmallButton("Open other file...")) {
            char path[512] = "";
            if (file_dialog_open(g_simple_mode ? "Open Stage" : "Open BDB/BDD",
                g_simple_mode
                    ? "Stage Files\0*.BDB;*.bdb;*.BDD;*.bdd\0All Files\0*.*\0"
                    : "Midway Background Files\0*.BDB;*.bdb;*.BDD;*.bdd\0All Files\0*.*\0",
                path, sizeof path))
            {
                welcome_open_path(path);
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        /* --- right column: start fresh --- */
        ImGui::BeginChild("##wright", ImVec2(col_right, H - 110.0f), false);
        ImGui::TextColored(ImVec4(0.7f,0.9f,1.0f,1.0f), "Start Fresh");
        ImGui::Spacing();
        float card_bw = col_right - 4;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        for (int c2 = 0; c2 < 4; c2++) {
            ImGui::PushID(100 + c2);
            bool blank = (c2 == 3);
            bool active = blank && g_card_blank_open;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f,0.45f,0.65f,1.0f));
            if (ImGui::Button("##wcard", ImVec2(card_bw, 38))) {
                if (blank) {
                    g_card_blank_open = !g_card_blank_open;
                } else {
                    int ti = g_cards[c2].ti;
                    g_new_template = ti;
                    snprintf(g_new_name, sizeof g_new_name, "%s", g_templates[ti].name);
                    g_new_w = g_templates[ti].w; g_new_h = g_templates[ti].h;
                    g_new_depth = g_templates[ti].depth; g_new_pals = g_templates[ti].pals;
                    request_unsaved_action(UNSAVED_ACTION_APPLY_NEW_PROJECT);
                }
            }
            if (active) ImGui::PopStyleColor();
            ImVec2 bmin2 = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(bmin2.x + 5, bmin2.y + 4),
                IM_COL32(220,220,255,255), g_cards[c2].title);
            /* show first line of desc only (desc uses \n as line separator) */
            const char *d = g_cards[c2].desc;
            const char *dnl = strchr(d, '\n');
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(bmin2.x + 5, bmin2.y + 20),
                IM_COL32(150,150,190,200), d, dnl ? dnl : NULL);
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        if (g_card_blank_open) {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(80); ImGui::InputInt("W##bw", &g_new_w); ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::InputInt("H##bh", &g_new_h);
            if (g_new_w < 1) g_new_w = 1; if (g_new_h < 1) g_new_h = 1;
            if (ImGui::Button("Create##blank", ImVec2(card_bw, 0))) {
                g_new_depth = 255; g_new_pals = 0;
                snprintf(g_new_name, sizeof g_new_name, "WORLD");
                request_unsaved_action(UNSAVED_ACTION_APPLY_NEW_PROJECT);
                g_card_blank_open = false;
            }
        }
        ImGui::EndChild();

        /* --- bottom bar --- */
        ImGui::Separator();
        bool hide_welcome = !g_welcome_show;
        if (ImGui::Checkbox("Don't show on startup", &hide_welcome)) {
            g_welcome_show = !hide_welcome;
            settings_save();
        }
    } else {
        /* ---- Advanced mode: two-column welcome with inline recent files ---- */
        const float W = 720.0f, H = 400.0f;
        ImGui::SetNextWindowPos(ImVec2(c.x/2 - W/2, c.y/2 - H/2));
        ImGui::SetNextWindowSize(ImVec2(W, H));
        ImGui::SetNextWindowFocus();
        ImGui::Begin("##welcome", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "midway-bddtool");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("  Background Editor for Midway Arcade Games");
        ImGui::Separator();
        ImGui::Spacing();

        float col_left  = W * 0.55f - ImGui::GetStyle().ItemSpacing.x;
        float col_right = W * 0.42f;
        float inner_h   = H - 100.0f;

        /* --- left column: recent files ---- */
        ImGui::BeginChild("##awleft", ImVec2(col_left, inner_h), false);
        ImGui::TextColored(ImVec4(0.7f,0.9f,1.0f,1.0f), "Recent Files");
        ImGui::Spacing();
        if (g_recent_count == 0) {
            ImGui::TextDisabled("No recent files yet.");
        } else {
            for (int ri = 0; ri < g_recent_count && ri < 8; ri++) {
                const char *p = g_recent_files[ri];
                char age[32]; welcome_rel_time(p, age, sizeof age);

                /* truncate the path from the left so it never runs into the
                   right-aligned age label */
                float age_w = age[0] ? ImGui::CalcTextSize(age).x : 0.0f;
                float pad_x = ImGui::GetStyle().FramePadding.x;
                float avail = (col_left - 4.0f) - 2.0f * pad_x - age_w - 14.0f;
                char display[160];
                snprintf(display, sizeof display, "%s", p);
                size_t plen = strlen(p);
                size_t start = 0;
                while (ImGui::CalcTextSize(display).x > avail && start + 4 < plen) {
                    start += 2;
                    snprintf(display, sizeof display, "...%s", p + start);
                }
                char lbl[200]; snprintf(lbl, sizeof lbl, "%s##arec%d", display, ri);
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f,0.20f,0.28f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f,0.30f,0.46f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.33f,0.40f,0.58f,1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                if (ImGui::Button(lbl, ImVec2(col_left - 4, 26)))
                    welcome_open_path(p);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                if (age[0]) {
                    ImVec2 bmax = ImGui::GetItemRectMax();
                    ImVec2 bmin = ImGui::GetItemRectMin();
                    float tw = ImGui::CalcTextSize(age).x;
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(bmax.x - tw - 6, bmin.y + 6),
                        IM_COL32(120,120,155,200), age);
                }
            }
        }
        ImGui::Spacing();
        if (ImGui::SmallButton("Open other file...")) {
            char path[512] = "";
            if (file_dialog_open("Open BDB/BDD",
                "Midway Background Files\0*.BDB;*.bdb;*.BDD;*.bdd\0All Files\0*.*\0",
                path, sizeof path))
                welcome_open_path(path);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        /* --- right column: actions + templates ---- */
        ImGui::BeginChild("##awright", ImVec2(col_right, inner_h), false);
        ImGui::TextColored(ImVec4(0.7f,0.9f,1.0f,1.0f), "Start");
        ImGui::Spacing();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("New Project...", ImVec2(col_right - 4, 30)))
            request_unsaved_action(UNSAVED_ACTION_SHOW_NEW_PROJECT);
        ImGui::Spacing();

        ImGui::TextDisabled("Templates:");
        float card_bw = col_right - 4;
        for (int ci = 0; ci < 3; ci++) {
            ImGui::PushID(200 + ci);
            if (ImGui::Button("##awcard", ImVec2(card_bw, 36))) {
                int ti = g_cards[ci].ti;
                g_new_template = ti;
                snprintf(g_new_name, sizeof g_new_name, "%s", g_templates[ti].name);
                g_new_w = g_templates[ti].w; g_new_h = g_templates[ti].h;
                g_new_depth = g_templates[ti].depth; g_new_pals = g_templates[ti].pals;
                request_unsaved_action(UNSAVED_ACTION_APPLY_NEW_PROJECT);
            }
            ImVec2 bmin2 = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(bmin2.x + 6, bmin2.y + 4),
                IM_COL32(215,215,255,255), g_cards[ci].title);
            const char *d = g_cards[ci].desc;
            const char *dnl = strchr(d, '\n');
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(bmin2.x + 6, bmin2.y + 20),
                IM_COL32(150,150,190,200), d, dnl ? dnl : NULL);
            ImGui::PopID();
        }
        ImGui::PopStyleVar();

        ImGui::EndChild();

        /* --- bottom bar ---- */
        ImGui::Separator();
        ImGui::TextDisabled("Quick start: New Project -> Import -> Place -> Drag -> Save");
        const char *cb_lbl = "Don't show on startup";
        float cb_w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x
                   + ImGui::CalcTextSize(cb_lbl).x;
        float cbx = ImGui::GetWindowWidth() - cb_w - ImGui::GetStyle().WindowPadding.x;
        ImGui::SameLine();
        if (cbx > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(cbx);
        bool hide_welcome2 = !g_welcome_show;
        if (ImGui::Checkbox(cb_lbl, &hide_welcome2)) {
            g_welcome_show = !hide_welcome2;
            settings_save();
        }
    }

    ImGui::PopStyleColor();
    ImGui::End();
}

