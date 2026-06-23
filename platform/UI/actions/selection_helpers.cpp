#include "Core/editor_project_globals.h"
#include "UI/actions/selection_helpers.h"

int selected_count(void)
{
    int count = 0;
    for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) count++;
    return count;
}

static int first_selected_object(void)
{
    for (int i = 0; i < g_no; i++)
        if (g_sel_flags[i])
            return i;
    return -1;
}

void toggle_object_selection(int idx)
{
    if (idx < 0 || idx >= g_no) return;
    if (g_sel_flags[idx]) {
        g_sel_flags[idx] = 0;
        if (g_hl_obj == idx)
            g_hl_obj = first_selected_object();
    } else {
        g_sel_flags[idx] = 1;
        g_hl_obj = idx;
    }
}

/* Hide/show are session-only editor preferences, not project data (consistent
 * with the Layers panel's Hide All/Show All, which also skip undo). Mainly
 * for decluttering: hide everything but a sparse ctrl-click selection so it's
 * obvious at a glance what a new module's bounding box will and won't cover. */
void hide_unselected_objects(void)
{
    for (int i = 0; i < g_no; i++)
        if (!g_sel_flags[i]) g_obj_hidden[i] = 1;
    g_view_changed = 1;
}

void show_all_objects(void)
{
    for (int i = 0; i < g_no; i++) g_obj_hidden[i] = 0;
    g_view_changed = 1;
}
