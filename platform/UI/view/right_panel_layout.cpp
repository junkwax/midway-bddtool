#include "bg_editor_globals.h"
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <cstring>

bool g_dock_right_panels_next = false;

struct RightPanelState {
    bool initialized;
    bool docked;
    bool collapsed;
    float height;
    float width;
    float last_y;
};

static RightPanelState g_right_panels[RIGHT_PANEL_COUNT] = {};
static int g_right_panel_order[RIGHT_PANEL_COUNT] = {
    RIGHT_PANEL_OBJECTS,
    RIGHT_PANEL_IMAGES,
    RIGHT_PANEL_PALETTES,
    RIGHT_PANEL_MODULES,
    RIGHT_PANEL_OBJ_PROPERTIES
};
static int  g_right_panel_drag_id = -1;
static bool g_right_panel_layout_loaded = false;
static bool g_right_panel_layout_dirty = false;

static float right_panel_min_width(int id)
{
    switch (id) {
        case RIGHT_PANEL_IMAGES:  return 440.0f;
        case RIGHT_PANEL_PALETTES:return 380.0f;
        case RIGHT_PANEL_OBJECTS: return 360.0f;
        case RIGHT_PANEL_MODULES: return 360.0f;
        default:                  return 320.0f;
    }
}

static float right_panel_default_width(int id)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = ds.x * 0.19f;
    if (w < 280.0f) w = 280.0f;
    if (w > 360.0f) w = 360.0f;
    float min_w = right_panel_min_width(id);
    if (w < min_w) w = min_w;
    return w;
}

static float right_panel_min_height_preferred(int id)
{
    switch (id) {
        case RIGHT_PANEL_IMAGES:  return 300.0f;
        case RIGHT_PANEL_PALETTES:return 250.0f;
        case RIGHT_PANEL_OBJECTS: return 210.0f;
        case RIGHT_PANEL_MODULES: return 230.0f;
        default:                  return 180.0f;
    }
}

static float right_panel_max_width(void)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = ds.x * 0.42f;
    if (w < 360.0f) w = 360.0f;
    if (w > 640.0f) w = 640.0f;
    return w;
}

static float right_panel_width_for(int id)
{
    float w = right_panel_default_width(id);
    if (id >= 0 && id < RIGHT_PANEL_COUNT && g_right_panels[id].width > 0.0f)
        w = g_right_panels[id].width;
    float min_w = right_panel_min_width(id);
    float max_w = right_panel_max_width();
    if (max_w < min_w) max_w = min_w;
    if (w < min_w) w = min_w;
    if (w > max_w) w = max_w;
    return w;
}

static float right_panel_dock_right_x(void)
{
    return ImGui::GetIO().DisplaySize.x - 24.0f;
}

static void right_panel_default_order(void)
{
    int defaults[RIGHT_PANEL_COUNT] = {
        RIGHT_PANEL_OBJECTS,
        RIGHT_PANEL_IMAGES,
        RIGHT_PANEL_PALETTES,
        RIGHT_PANEL_MODULES,
        RIGHT_PANEL_OBJ_PROPERTIES
    };
    std::memcpy(g_right_panel_order, defaults, sizeof g_right_panel_order);
}

static bool right_panel_visible(int id)
{
    switch (id) {
        case RIGHT_PANEL_OBJECTS:
            return true;
        case RIGHT_PANEL_OBJ_PROPERTIES:
            return false;
        case RIGHT_PANEL_PALETTES:
            return true;
        case RIGHT_PANEL_IMAGES:
            return g_show_images;
        case RIGHT_PANEL_MODULES:
            return g_show_modules;
        default:
            return false;
    }
}

static float right_panel_base_height(int id)
{
    switch (id) {
        case RIGHT_PANEL_OBJ_PROPERTIES: return 0.0f;
        case RIGHT_PANEL_PALETTES:       return 300.0f;
        case RIGHT_PANEL_IMAGES:         return 390.0f;
        case RIGHT_PANEL_MODULES:        return 280.0f;
        case RIGHT_PANEL_OBJECTS:        return 240.0f;
        default:                         return 180.0f;
    }
}

static float right_panel_min_height(int id)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float top = editor_canvas_top_y();
    float bottom = 28.0f;
    float gap = 8.0f;
    float avail = ds.y - top - bottom;
    if (avail < 220.0f) avail = 220.0f;

    int visible_n = 0;
    float total_min = 0.0f;
    for (int oi = 0; oi < RIGHT_PANEL_COUNT; oi++) {
        int pid = g_right_panel_order[oi];
        if (pid < 0 || pid >= RIGHT_PANEL_COUNT) continue;
        if (!right_panel_visible(pid) || !g_right_panels[pid].docked) continue;
        total_min += right_panel_min_height_preferred(pid);
        visible_n++;
    }

    float preferred = right_panel_min_height_preferred(id);
    float gap_total = visible_n > 1 ? gap * (float)(visible_n - 1) : 0.0f;
    if (total_min + gap_total <= avail || total_min <= 0.0f)
        return preferred;

    float scale = (avail - gap_total) / total_min;
    if (scale < 0.55f) scale = 0.55f;
    float h = preferred * scale;
    if (h < 96.0f) h = 96.0f;
    return h;
}

static float right_panel_default_height(int id)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float top = editor_canvas_top_y();
    float bottom = 28.0f;
    float gap = 8.0f;
    float avail = ds.y - top - bottom;
    if (avail < 260.0f) avail = 260.0f;

    int visible_n = 0;
    float total = 0.0f;
    for (int oi = 0; oi < RIGHT_PANEL_COUNT; oi++) {
        int pid = g_right_panel_order[oi];
        if (pid < 0 || pid >= RIGHT_PANEL_COUNT) continue;
        if (!right_panel_visible(pid) || !g_right_panels[pid].docked) continue;
        total += right_panel_base_height(pid);
        visible_n++;
    }
    float gap_total = visible_n > 1 ? gap * (float)(visible_n - 1) : 0.0f;
    float scale = (total + gap_total > avail && total > 0.0f)
                ? (avail - gap_total) / total
                : 1.0f;
    if (scale < 0.35f) scale = 0.35f;

    float h = right_panel_base_height(id) * scale;
    float min_h = right_panel_min_height(id);
    if (h < min_h) h = min_h;
    return h;
}

static float right_panel_height_for_layout(int id)
{
    if (id < 0 || id >= RIGHT_PANEL_COUNT) return 0.0f;
    float h = g_right_panels[id].height;
    float min_h = right_panel_min_height(id);
    if (h < min_h) h = right_panel_default_height(id);
    if (h < min_h) h = min_h;
    return h;
}

/* Height a panel occupies in the docked stack. A collapsed panel only takes its
   title bar, so panels below it slide up to close the gap. */
static float right_panel_stack_height(int id)
{
    if (id < 0 || id >= RIGHT_PANEL_COUNT) return 0.0f;
    if (g_right_panels[id].collapsed)
        return ImGui::GetFrameHeight();
    return right_panel_height_for_layout(id);
}

static float right_panel_dock_x(int id)
{
    float w = right_panel_width_for(id);
    float x = right_panel_dock_right_x() - w;
    return x < 8.0f ? 8.0f : x;
}

static float right_panel_y_for(int id)
{
    float y = editor_canvas_top_y();
    float gap = 8.0f;
    for (int oi = 0; oi < RIGHT_PANEL_COUNT; oi++) {
        int pid = g_right_panel_order[oi];
        if (pid == id) break;
        if (pid < 0 || pid >= RIGHT_PANEL_COUNT) continue;
        if (!right_panel_visible(pid) || !g_right_panels[pid].docked) continue;
        y += right_panel_stack_height(pid) + gap;
    }
    return y;
}

static void right_panel_promote_object_group_order(void)
{
    int out[RIGHT_PANEL_COUNT];
    int n = 0;
    out[n++] = RIGHT_PANEL_OBJECTS;
    for (int i = 0; i < RIGHT_PANEL_COUNT; i++) {
        int id = g_right_panel_order[i];
        if (id == RIGHT_PANEL_OBJECTS || id == RIGHT_PANEL_OBJ_PROPERTIES)
            continue;
        if (id >= 0 && id < RIGHT_PANEL_COUNT)
            out[n++] = id;
    }
    while (n < RIGHT_PANEL_COUNT)
        out[n++] = RIGHT_PANEL_OBJ_PROPERTIES;
    std::memcpy(g_right_panel_order, out, sizeof g_right_panel_order);
}

static void right_panel_repair_saved_layout(void)
{
    bool repaired = false;
    for (int i = 0; i < RIGHT_PANEL_COUNT; i++) {
        float min_h = right_panel_min_height(i);
        if (g_right_panels[i].docked &&
            g_right_panels[i].height > 0.0f &&
            g_right_panels[i].height < min_h) {
            g_right_panels[i].height = 0.0f;
            repaired = true;
        }

        float min_w = right_panel_min_width(i);
        if (g_right_panels[i].docked &&
            g_right_panels[i].width > 0.0f &&
            g_right_panels[i].width < min_w) {
            g_right_panels[i].width = min_w;
            repaired = true;
        }
    }
    if (repaired)
        g_right_panel_layout_dirty = true;
}

static void right_panel_layout_load(void)
{
    if (g_right_panel_layout_loaded) return;
    g_right_panel_layout_loaded = true;

    right_panel_default_order();
    for (int i = 0; i < RIGHT_PANEL_COUNT; i++) {
        g_right_panels[i].initialized = true;
        g_right_panels[i].docked = true;
        g_right_panels[i].height = 0.0f;
        g_right_panels[i].width = 0.0f;
        g_right_panels[i].last_y = 0.0f;
    }

    FILE *f = std::fopen("bddview_right_panels.cfg", "r");
    if (!f) return;

    char tag[32] = "";
    int ver = 0;
    if (std::fscanf(f, "%31s %d", tag, &ver) != 2 ||
        std::strcmp(tag, "RIGHTPANELS") != 0 || ver != 1) {
        std::fclose(f);
        return;
    }

    while (std::fscanf(f, "%31s", tag) == 1) {
        if (std::strcmp(tag, "order") == 0) {
            int tmp[RIGHT_PANEL_COUNT];
            bool seen[RIGHT_PANEL_COUNT] = {};
            bool ok = true;
            for (int i = 0; i < RIGHT_PANEL_COUNT; i++) {
                if (std::fscanf(f, "%d", &tmp[i]) != 1 ||
                    tmp[i] < 0 || tmp[i] >= RIGHT_PANEL_COUNT || seen[tmp[i]]) {
                    ok = false;
                } else {
                    seen[tmp[i]] = true;
                }
            }
            if (ok) std::memcpy(g_right_panel_order, tmp, sizeof g_right_panel_order);
        } else if (std::strcmp(tag, "panel") == 0) {
            int id = -1, docked = 1;
            float h = 0.0f;
            if (std::fscanf(f, "%d %d %f", &id, &docked, &h) == 3 &&
                id >= 0 && id < RIGHT_PANEL_COUNT) {
                g_right_panels[id].initialized = true;
                g_right_panels[id].docked = docked != 0;
                g_right_panels[id].height = h > 0.0f ? h : 0.0f;
            }
        } else if (std::strcmp(tag, "panelwidth") == 0) {
            int id = -1;
            float w = 0.0f;
            if (std::fscanf(f, "%d %f", &id, &w) == 2 &&
                id >= 0 && id < RIGHT_PANEL_COUNT)
                g_right_panels[id].width = w > 0.0f ? w : 0.0f;
        } else if (std::strcmp(tag, "width") == 0) {
            float ignored = 0.0f;
            std::fscanf(f, "%f", &ignored);
        }
    }
    std::fclose(f);
    right_panel_promote_object_group_order();
    right_panel_repair_saved_layout();
}

static void right_panel_layout_save(void)
{
    FILE *f = std::fopen("bddview_right_panels.cfg", "w");
    if (!f) return;
    std::fprintf(f, "RIGHTPANELS 1\n");
    std::fprintf(f, "order");
    for (int i = 0; i < RIGHT_PANEL_COUNT; i++)
        std::fprintf(f, " %d", g_right_panel_order[i]);
    std::fprintf(f, "\n");
    for (int i = 0; i < RIGHT_PANEL_COUNT; i++) {
        std::fprintf(f, "panel %d %d %.1f\n", i,
                     g_right_panels[i].docked ? 1 : 0,
                     g_right_panels[i].height);
        std::fprintf(f, "panelwidth %d %.1f\n", i,
                     g_right_panels[i].width);
    }
    std::fclose(f);
}

static void right_panel_reset_defaults(void)
{
    right_panel_default_order();
    for (int i = 0; i < RIGHT_PANEL_COUNT; i++) {
        g_right_panels[i].initialized = true;
        g_right_panels[i].docked = true;
        g_right_panels[i].height = 0.0f;
        g_right_panels[i].width = 0.0f;
        g_right_panels[i].last_y = 0.0f;
    }
    g_right_panel_drag_id = -1;
    g_right_panel_layout_dirty = true;
}

void right_panel_frame_begin(void)
{
    right_panel_layout_load();
    if (g_dock_right_panels_next)
        right_panel_reset_defaults();
}

void right_panel_frame_end(void)
{
    if (g_right_panel_layout_dirty && !ImGui::GetIO().MouseDown[0]) {
        right_panel_layout_save();
        g_right_panel_layout_dirty = false;
    }
}

float right_panel_canvas_right_limit(void)
{
    right_panel_layout_load();
    float limit = ImGui::GetIO().DisplaySize.x - 6.0f;
    for (int oi = 0; oi < RIGHT_PANEL_COUNT; oi++) {
        int id = g_right_panel_order[oi];
        if (id < 0 || id >= RIGHT_PANEL_COUNT) continue;
        if (!right_panel_visible(id) || !g_right_panels[id].docked) continue;
        float x = right_panel_dock_x(id);
        if (x < limit)
            limit = x;
    }
    return limit;
}

static void right_panel_reorder_by_y(int id, float y)
{
    if (id < 0 || id >= RIGHT_PANEL_COUNT) return;

    int compact[RIGHT_PANEL_COUNT];
    int n = 0;
    for (int i = 0; i < RIGHT_PANEL_COUNT; i++)
        if (g_right_panel_order[i] != id)
            compact[n++] = g_right_panel_order[i];

    int insert_at = n;
    for (int i = 0; i < n; i++) {
        int pid = compact[i];
        if (pid < 0 || pid >= RIGHT_PANEL_COUNT) continue;
        if (!right_panel_visible(pid) || !g_right_panels[pid].docked) continue;
        float mid = g_right_panels[pid].last_y + right_panel_height_for_layout(pid) * 0.5f;
        if (y < mid) {
            insert_at = i;
            break;
        }
    }

    int out[RIGHT_PANEL_COUNT];
    int oi = 0;
    for (int i = 0; i <= n; i++) {
        if (i == insert_at) out[oi++] = id;
        if (i < n) out[oi++] = compact[i];
    }
    std::memcpy(g_right_panel_order, out, sizeof g_right_panel_order);
    g_right_panel_layout_dirty = true;
}

static bool right_panel_near_rail(ImVec2 pos, ImVec2 size)
{
    float dock_right = right_panel_dock_right_x();
    float snap = 90.0f;
    return std::fabs((pos.x + size.x) - dock_right) <= snap;
}

static bool right_panel_titlebar_hit(ImVec2 pos, ImVec2 size)
{
    ImGuiIO &io = ImGui::GetIO();
    float title_h = ImGui::GetFrameHeight();
    return io.MousePos.x >= pos.x && io.MousePos.x <= pos.x + size.x &&
           io.MousePos.y >= pos.y && io.MousePos.y <= pos.y + title_h;
}

void right_panel_set_next(int id)
{
    if (id < 0 || id >= RIGHT_PANEL_COUNT) return;
    right_panel_layout_load();

    RightPanelState *st = &g_right_panels[id];
    if (!st->initialized) {
        st->initialized = true;
        st->docked = true;
        st->height = 0.0f;
        st->width = 0.0f;
    }

    float w = right_panel_width_for(id);
    float h = right_panel_height_for_layout(id);
    float min_h = right_panel_min_height(id);
    if (st->docked && st->height > 0.0f && st->height < min_h) {
        st->height = 0.0f;
        g_right_panel_layout_dirty = true;
    }
    if (st->docked && g_right_panel_drag_id != id) {
        ImGui::SetNextWindowPos(ImVec2(right_panel_dock_x(id), right_panel_y_for(id)),
                                ImGuiCond_Always);
        float max_w = right_panel_max_width();
        if (max_w < right_panel_min_width(id))
            max_w = right_panel_min_width(id);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(right_panel_min_width(id), min_h),
            ImVec2(max_w, ImGui::GetIO().DisplaySize.y));
        if (st->height <= 0.0f)
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        else
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
    } else if (st->height <= 0.0f) {
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    }
}

void right_panel_after_begin(int id)
{
    if (id < 0 || id >= RIGHT_PANEL_COUNT) return;
    RightPanelState *st = &g_right_panels[id];
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    if (right_panel_titlebar_hit(pos, size) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        g_right_panel_drag_id = id;

    if (g_right_panel_drag_id == id && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        if (!right_panel_near_rail(pos, size))
            st->docked = false;
    }

    if (g_right_panel_drag_id == id && !io.MouseDown[0]) {
        if (right_panel_near_rail(pos, size)) {
            st->docked = true;
            right_panel_reorder_by_y(id, pos.y);
        } else {
            st->docked = false;
            g_right_panel_layout_dirty = true;
        }
        g_right_panel_drag_id = -1;
    }

    bool collapsed = ImGui::IsWindowCollapsed();
    st->collapsed = collapsed;
    /* Only record the expanded height; a collapsed panel reports its title-bar
       height, which we must not persist as the panel's real size. */
    float min_h = right_panel_min_height(id);
    if (!collapsed && st->docked && size.y < min_h - 0.5f) {
        st->height = 0.0f;
        g_right_panel_layout_dirty = true;
    } else if (!collapsed && std::fabs(st->height - size.y) > 0.5f) {
        st->height = size.y;
        g_right_panel_layout_dirty = true;
    }
    if (!collapsed && st->docked && g_right_panel_drag_id != id) {
        float next_w = size.x;
        float min_w = right_panel_min_width(id);
        float max_w = right_panel_max_width();
        if (max_w < min_w) max_w = min_w;
        if (next_w < min_w) next_w = min_w;
        if (next_w > max_w) next_w = max_w;
        if (std::fabs(st->width - next_w) > 0.5f) {
            st->width = next_w;
            g_right_panel_layout_dirty = true;
        }
    }
    st->last_y = pos.y;
}

void set_left_panel_default(float y, float w, float h)
{
    ImGuiCond cond = g_dock_right_panels_next ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    if (y < 60.0f) y = 60.0f;
    ImGui::SetNextWindowPos(ImVec2(8.0f, y), cond);
    ImGui::SetNextWindowSize(ImVec2(w, h), cond);
}
