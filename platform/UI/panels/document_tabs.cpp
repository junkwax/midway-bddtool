#include "UI/panels/DocumentTabsPanel.h"
#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_commands.h"

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
    runtime_guides_clear_session();
    bg_editor_autoload_lod_assets();
    runtime_actor_autoload_for_stage();
    g_dirty = d->dirty;
    g_need_rebuild = 1;
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
    static int s_last_selected_tab_id = 0;

    ImGuiTabBarFlags tab_flags =
        ImGuiTabBarFlags_FittingPolicyScroll |
        ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
    if (ImGui::BeginTabBar("##document_tabbar", tab_flags)) {
        if (g_num_docs < MAX_DOCS &&
            ImGui::TabItemButton("+", ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoTooltip))
            new_doc_requested = true;

        int active_tab_id = (g_cur_doc >= 0 && g_cur_doc < g_num_docs)
                          ? g_docs[g_cur_doc].tab_id : 0;
        for (int i = 0; i < g_num_docs; i++) {
            if (!g_docs[i].loaded) continue;

            char label[128];
            bool active = (i == g_cur_doc);
            bool is_dirty = active ? g_dirty : g_docs[i].dirty;
            const char *tab_name = (active && g_name[0]) ? g_name :
                (g_docs[i].snapshot.name[0] ? g_docs[i].snapshot.name : "Untitled");
            snprintf(label, sizeof label, "%s###doc%d", tab_name,
                     g_docs[i].tab_id > 0 ? g_docs[i].tab_id : i + 1);

            bool tab_open = true;
            ImGuiTabItemFlags item_flags = ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;
            if (is_dirty)
                item_flags |= ImGuiTabItemFlags_UnsavedDocument | ImGuiTabItemFlags_NoAssumedClosure;
            if (active && active_tab_id != s_last_selected_tab_id)
                item_flags |= ImGuiTabItemFlags_SetSelected;

            if (ImGui::BeginTabItem(label, &tab_open, item_flags)) {
                if (i != g_cur_doc)
                    switch_to = i;
                int tab_no = active ? g_no : g_docs[i].snapshot.no;
                int tab_ni = active ? g_ni : g_docs[i].snapshot.ni;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%d objects, %d images", tab_no, tab_ni);
                ImGui::EndTabItem();
            }
            if (!tab_open)
                close_idx = i;
        }
        ImGui::EndTabBar();
    }

    if (close_idx >= 0) {
        bool close_dirty = (close_idx == g_cur_doc) ? (g_dirty != 0) : g_docs[close_idx].dirty;
        if (close_dirty)
            editor_emit_unsaved_action(UNSAVED_ACTION_CLOSE_DOC, nullptr, close_idx);
        else
            doc_close(close_idx);
    } else if (switch_to >= 0 && switch_to != g_cur_doc && switch_to < g_num_docs) {
        if (g_cur_doc >= 0)
            doc_save(g_cur_doc);
        doc_restore(switch_to);
    }
    if (new_doc_requested) {
        editor_emit_unsaved_action(UNSAVED_ACTION_SHOW_NEW_PROJECT);
    }

    if (g_cur_doc >= 0 && g_cur_doc < g_num_docs)
        s_last_selected_tab_id = g_docs[g_cur_doc].tab_id;
    else
        s_last_selected_tab_id = 0;
}

void DocumentTabsPanel::render()
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
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

