#include "bg_editor_globals.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
Document g_docs[MAX_DOCS];
int g_num_docs = 0;
int g_cur_doc = -1;
int g_next_doc_id = 1;
bool g_docs_init = false;
bool g_show_new = false;

static void workspace_clear(void)
{
    editor_project_reset_loaded_stage();
    g_dirty = 0;
    g_need_rebuild = 1;
    g_view_changed = 1;
}

static void doc_free(int idx)
{
    if (idx < 0 || idx >= MAX_DOCS) return;
    project_snapshot_release(&g_docs[idx].snapshot);
    memset(&g_docs[idx], 0, sizeof(Document));
}

void doc_save(int idx)
{
    if (idx < 0 || idx >= MAX_DOCS) return;
    Document *d = &g_docs[idx];
    if (!project_snapshot_capture_current(&d->snapshot, true)) return;
    memcpy(d->bdb_path, g_bdb_path, sizeof g_bdb_path);
    memcpy(d->bdd_path, g_bdd_path, sizeof g_bdd_path);
    d->dirty = g_dirty;
    d->loaded = true;
}

void doc_restore(int idx)
{
    Document *d = &g_docs[idx];
    int object_cap = editor_project_object_capacity();
    if (!d->loaded)
        return;
    project_snapshot_restore_current(&d->snapshot, true);
    g_dirty = d->dirty;
    memcpy(g_bdb_path, d->bdb_path, sizeof g_bdb_path);
    memcpy(g_bdd_path, d->bdd_path, sizeof g_bdd_path);
    g_cur_doc = idx;
    g_need_rebuild = 1;
    g_hl_obj = -1;
    editor_project_clear_selection();
}

void doc_add(void)
{
    if (g_num_docs >= MAX_DOCS) return;
    if (g_cur_doc >= 0 && g_cur_doc < MAX_DOCS) doc_save(g_cur_doc);
    g_cur_doc = g_num_docs;
    memset(&g_docs[g_cur_doc], 0, sizeof(Document));
    g_docs[g_cur_doc].tab_id = g_next_doc_id++;
    g_num_docs++;
}

void doc_close(int idx)
{
    if (idx < 0 || idx >= g_num_docs) return;

    bool closing_current = (idx == g_cur_doc);
    if (g_cur_doc >= 0 && g_cur_doc < g_num_docs)
        doc_save(g_cur_doc);

    doc_free(idx);
    for (int j = idx; j < g_num_docs - 1; j++)
        memcpy(&g_docs[j], &g_docs[j + 1], sizeof(Document));
    memset(&g_docs[g_num_docs - 1], 0, sizeof(Document));
    g_num_docs--;

    if (g_num_docs <= 0) {
        g_num_docs = 0;
        g_cur_doc = -1;
        workspace_clear();
        return;
    }

    if (g_cur_doc > idx)
        g_cur_doc--;
    else if (closing_current)
        g_cur_doc = (idx < g_num_docs) ? idx : (g_num_docs - 1);

    if (g_cur_doc >= 0)
        doc_restore(g_cur_doc);
}

static void doc_tabs(void)
{
    if (g_num_docs < 1) return;
    bool new_doc_requested = false;
    int switch_to = -1;
    int close_idx = -1;

    if (g_num_docs < MAX_DOCS && ImGui::SmallButton("+"))
        new_doc_requested = true;

    for (int i = 0; i < g_num_docs; i++) {
        if (!g_docs[i].loaded) continue;
        ImGui::SameLine(0, 4);
        ImGui::PushID(g_docs[i].tab_id > 0 ? g_docs[i].tab_id : i + 1);

        char label[96];
        bool active = (i == g_cur_doc);
        bool is_dirty = active ? g_dirty : g_docs[i].dirty;
        int tab_no = active ? g_no : g_docs[i].snapshot.no;
        int tab_ni = active ? g_ni : g_docs[i].snapshot.ni;
        const char *tab_name = (active && g_name[0]) ? g_name :
            (g_docs[i].snapshot.name[0] ? g_docs[i].snapshot.name : "Untitled");
        if (active || g_num_docs == 1)
            snprintf(label, sizeof label, "%s%s  %d obj  %d img",
                     tab_name, is_dirty ? " *" : "", tab_no, tab_ni);
        else
            snprintf(label, sizeof label, "%s%s", tab_name, is_dirty ? " *" : "");

        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f,0.45f,0.80f,1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f,0.52f,0.90f,1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f,0.58f,1.00f,1.00f));
        }
        if (ImGui::SmallButton(label))
            switch_to = i;
        if (active) ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 1);
        if (ImGui::SmallButton("x"))
            close_idx = i;
        ImGui::PopID();
    }

    if (close_idx >= 0) {
        bool close_dirty = (close_idx == g_cur_doc) ? (g_dirty != 0) : g_docs[close_idx].dirty;
        if (close_dirty)
            request_unsaved_action(UNSAVED_ACTION_CLOSE_DOC, NULL, close_idx);
        else
            doc_close(close_idx);
    } else if (switch_to >= 0 && switch_to != g_cur_doc && switch_to < g_num_docs) {
        if (g_cur_doc >= 0)
            doc_save(g_cur_doc);
        doc_restore(switch_to);
    }
    if (new_doc_requested) {
        request_unsaved_action(UNSAVED_ACTION_SHOW_NEW_PROJECT);
    }
}

static void draw_doc_strip_summary(void)
{
    if (!g_have_bdb && g_ni == 0) return;

    int sel = selected_count();
    char info[160];
    if (g_name[0]) {
        snprintf(info, sizeof info, "%s%d objects  %d images  %d palettes%s",
                 g_dirty ? "*  " : "", g_no, g_ni, g_n_pals,
                 sel > 0 ? "  |" : "");
    } else {
        snprintf(info, sizeof info, "%s%d images  %d palettes%s",
                 g_dirty ? "*  " : "", g_ni, g_n_pals,
                 sel > 0 ? "  |" : "");
    }
    ImGui::SameLine(0, 12);
    if (g_dirty) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.8f,0.25f,1.0f));
    ImGui::TextDisabled("%s", info);
    if (g_dirty) ImGui::PopStyleColor();
    if (sel > 0) {
        ImGui::SameLine(0, 4);
        ImGui::TextColored(ImVec4(1.0f,0.85f,0.35f,1.0f), "%d selected", sel);
    }

    if (g_have_bdb && g_no > 0) {
        static const struct { int wx; const char *name; ImVec4 col; } layers[] = {
            {0x32, "32", ImVec4(1.00f,0.70f,0.39f,1.0f)},
            {0x3C, "3C", ImVec4(0.39f,0.78f,1.00f,1.0f)},
            {0x40, "40", ImVec4(0.39f,1.00f,0.47f,1.0f)},
            {0x41, "41", ImVec4(0.78f,1.00f,0.39f,1.0f)},
            {0x43, "43", ImVec4(1.00f,0.59f,0.78f,1.0f)},
            {0x46, "46", ImVec4(1.00f,0.39f,0.39f,1.0f)}
        };
        for (int li = 0; li < 6; li++) {
            int count = 0;
            for (int oi = 0; oi < g_no; oi++)
                if (((g_obj[oi].wx >> 8) & 0xFF) == layers[li].wx)
                    count++;
            if (count <= 0) continue;
            char pill[32];
            snprintf(pill, sizeof pill, "%s:%d", layers[li].name, count);
            ImGui::SameLine(0, 6);
            ImGui::TextColored(layers[li].col, "%s", pill);
        }
    }
}

void draw_doc_tab_strip(void)
{
    if (g_num_docs < 1) return;

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float menu_h = ImGui::GetFrameHeight();
    float toolbar_h = 38.0f;
    ImGui::SetNextWindowPos(ImVec2(0, menu_h + toolbar_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ds.x, 30.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.20f, 0.25f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
    bool open = ImGui::Begin("##document_tabs", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (open) {
        doc_tabs();
        draw_doc_strip_summary();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

