/* Fixed right sidebar.

   Everything that used to float in its own snap-docked rail window -- objects,
   images, palettes, modules, layers, minimap -- lives here in one pinned column.
   Each section is a tab, and each section's own crowded surface is split again
   into sub-tabs so no single view is a wall of controls. */

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/view/right_sidebar.h"
#include "UI/view/status_bar.h"
#include "UI/view/welcome_screen.h"
#include "UI/app/editor_lifecycle.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

static const char *SIDEBAR_CFG = "bddview_sidebar.cfg";

static const float SIDEBAR_GRIP_W      = 6.0f;
static const float SIDEBAR_COLLAPSED_W = 30.0f;
static const float SIDEBAR_MIN_W       = 300.0f;

static float g_sidebar_width     = 0.0f;   /* 0 means "use the display default" */
static bool  g_sidebar_collapsed = false;
static int   g_sidebar_tab       = SIDEBAR_TAB_OBJECTS;
static bool  g_sidebar_loaded    = false;
static bool  g_sidebar_dirty     = false;

static int   g_sidebar_req_tab   = -1;     /* section to select this frame */
static int   g_sidebar_req_sub   = -1;     /* sub-tab within that section */
static bool  g_sidebar_restore   = false;  /* re-select the saved section once */

static float sidebar_default_width(void)
{
    float w = ImGui::GetIO().DisplaySize.x * 0.24f;
    if (w < 340.0f) w = 340.0f;
    if (w > 460.0f) w = 460.0f;
    return w;
}

static float sidebar_max_width(void)
{
    float w = ImGui::GetIO().DisplaySize.x * 0.5f;
    if (w < SIDEBAR_MIN_W) w = SIDEBAR_MIN_W;
    if (w > 720.0f) w = 720.0f;
    return w;
}

static float sidebar_clamp_width(float w)
{
    float max_w = sidebar_max_width();
    if (w < SIDEBAR_MIN_W) w = SIDEBAR_MIN_W;
    if (w > max_w) w = max_w;
    return w;
}

static void sidebar_save(void)
{
    FILE *f = std::fopen(SIDEBAR_CFG, "w");
    if (!f) return;
    std::fprintf(f, "SIDEBAR 1\n");
    std::fprintf(f, "width %.1f\n", g_sidebar_width);
    std::fprintf(f, "collapsed %d\n", g_sidebar_collapsed ? 1 : 0);
    std::fprintf(f, "tab %d\n", g_sidebar_tab);
    std::fclose(f);
}

static void sidebar_load(void)
{
    if (g_sidebar_loaded) return;
    g_sidebar_loaded = true;

    FILE *f = std::fopen(SIDEBAR_CFG, "r");
    if (!f) return;

    char tag[32] = "";
    int ver = 0;
    if (std::fscanf(f, "%31s %d", tag, &ver) != 2 ||
        std::strcmp(tag, "SIDEBAR") != 0 || ver != 1) {
        std::fclose(f);
        return;
    }
    while (std::fscanf(f, "%31s", tag) == 1) {
        if (std::strcmp(tag, "width") == 0) {
            float w = 0.0f;
            if (std::fscanf(f, "%f", &w) == 1 && w > 0.0f)
                g_sidebar_width = w;
        } else if (std::strcmp(tag, "collapsed") == 0) {
            int c = 0;
            if (std::fscanf(f, "%d", &c) == 1)
                g_sidebar_collapsed = c != 0;
        } else if (std::strcmp(tag, "tab") == 0) {
            int t = 0;
            if (std::fscanf(f, "%d", &t) == 1 && t >= 0 && t < SIDEBAR_TAB_COUNT) {
                g_sidebar_tab = t;
                g_sidebar_restore = true;
            }
        }
    }
    std::fclose(f);
}

bool right_sidebar_visible(void)
{
    if (g_preview_mode) return false;
    if (welcome_visible()) return false;
    return true;
}

float right_sidebar_width(void)
{
    if (!right_sidebar_visible()) return 0.0f;
    sidebar_load();
    if (g_sidebar_collapsed) return SIDEBAR_COLLAPSED_W;
    if (g_sidebar_width <= 0.0f) return sidebar_default_width();
    return sidebar_clamp_width(g_sidebar_width);
}

float editor_canvas_right_x(void)
{
    float ds_x = ImGui::GetIO().DisplaySize.x;
    if (!right_sidebar_visible()) return ds_x;
    float x = ds_x - right_sidebar_width() - SIDEBAR_GRIP_W;
    float min_x = ds_x * 0.25f;
    if (x < min_x) x = min_x;
    return x;
}

void right_sidebar_reset_layout(void)
{
    sidebar_load();
    g_sidebar_width = 0.0f;
    g_sidebar_collapsed = false;
    g_sidebar_dirty = true;
}

void right_sidebar_show_tab(int tab, int sub_tab)
{
    if (tab < 0 || tab >= SIDEBAR_TAB_COUNT) return;
    g_sidebar_req_tab = tab;
    g_sidebar_req_sub = sub_tab;
    if (g_sidebar_collapsed) {
        g_sidebar_collapsed = false;
        g_sidebar_dirty = true;
    }
}

/* --- section plumbing ------------------------------------------------- */

static ImGuiTabItemFlags sidebar_tab_flags(int tab)
{
    return (g_sidebar_req_tab == tab) ? ImGuiTabItemFlags_SetSelected : 0;
}

static ImGuiTabItemFlags sidebar_sub_flags(int tab, int sub)
{
    return (g_sidebar_req_tab == tab && g_sidebar_req_sub == sub)
         ? ImGuiTabItemFlags_SetSelected : 0;
}

/* Each sub-tab body scrolls on its own so the tab strips stay put. */
static void sidebar_body(const char *id, void (*draw)(void))
{
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    draw();
    ImGui::EndChild();
}

static void sidebar_section_objects(void)
{
    if (!ImGui::BeginTabBar("sidebar_objects", ImGuiTabBarFlags_FittingPolicyScroll))
        return;
    if (ImGui::BeginTabItem("Properties", NULL, sidebar_sub_flags(SIDEBAR_TAB_OBJECTS, 0))) {
        sidebar_body("obj_props_body", draw_obj_properties_contents);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("List", NULL, sidebar_sub_flags(SIDEBAR_TAB_OBJECTS, 1))) {
        sidebar_body("obj_list_body", draw_obj_list_contents);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

static void sidebar_section_images(void)
{
    if (!ImGui::BeginTabBar("sidebar_images", ImGuiTabBarFlags_FittingPolicyScroll))
        return;
    if (ImGui::BeginTabItem("Assets", NULL, sidebar_sub_flags(SIDEBAR_TAB_IMAGES, 0))) {
        sidebar_body("img_assets_body", draw_image_list_assets_contents);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Tools", NULL, sidebar_sub_flags(SIDEBAR_TAB_IMAGES, 1))) {
        sidebar_body("img_tools_body", draw_image_list_tools_contents);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

static void sidebar_section_palettes(void)
{
    if (!ImGui::BeginTabBar("sidebar_palettes", ImGuiTabBarFlags_FittingPolicyScroll))
        return;
    if (ImGui::BeginTabItem("Colors", NULL, sidebar_sub_flags(SIDEBAR_TAB_PALETTES, 0))) {
        sidebar_body("pal_colors_body", draw_palette_colors_contents);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Slots", NULL, sidebar_sub_flags(SIDEBAR_TAB_PALETTES, 1))) {
        sidebar_body("pal_slots_body", draw_palette_slots_contents);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Tools", NULL, sidebar_sub_flags(SIDEBAR_TAB_PALETTES, 2))) {
        sidebar_body("pal_tools_body", draw_palette_tools_contents);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

static void sidebar_section_modules(void)
{
    if (!ImGui::BeginTabBar("sidebar_modules", ImGuiTabBarFlags_FittingPolicyScroll))
        return;
    if (ImGui::BeginTabItem("Overview", NULL, sidebar_sub_flags(SIDEBAR_TAB_MODULES, 0))) {
        sidebar_body("mod_overview_body", draw_modules_summary_contents);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Create", NULL, sidebar_sub_flags(SIDEBAR_TAB_MODULES, 1))) {
        sidebar_body("mod_create_body", draw_modules_create_contents);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Edit", NULL, sidebar_sub_flags(SIDEBAR_TAB_MODULES, 2))) {
        sidebar_body("mod_edit_body", draw_modules_edit_contents);
        ImGui::EndTabItem();
    }
    if (!g_simple_mode &&
        ImGui::BeginTabItem("Runtime", NULL, sidebar_sub_flags(SIDEBAR_TAB_MODULES, 3))) {
        sidebar_body("mod_runtime_body", draw_modules_runtime_contents);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

static void sidebar_section_stage(void)
{
    if (!ImGui::BeginTabBar("sidebar_stage", ImGuiTabBarFlags_FittingPolicyScroll))
        return;
    if (ImGui::BeginTabItem("Layers", NULL, sidebar_sub_flags(SIDEBAR_TAB_STAGE, 0))) {
        sidebar_body("stage_layers_body", draw_layers_contents);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Map", NULL, sidebar_sub_flags(SIDEBAR_TAB_STAGE, 1))) {
        sidebar_body("stage_map_body", draw_minimap_contents);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

static const char *sidebar_tab_label(int tab)
{
    switch (tab) {
        case SIDEBAR_TAB_OBJECTS:  return "Objects";
        case SIDEBAR_TAB_IMAGES:   return "Images";
        case SIDEBAR_TAB_PALETTES: return "Palettes";
        case SIDEBAR_TAB_MODULES:  return g_simple_mode ? "Regions" : "Modules";
        case SIDEBAR_TAB_STAGE:    return "Stage";
        default:                   return "?";
    }
}

static const char *sidebar_tab_initial(int tab)
{
    switch (tab) {
        case SIDEBAR_TAB_OBJECTS:  return "O";
        case SIDEBAR_TAB_IMAGES:   return "I";
        case SIDEBAR_TAB_PALETTES: return "P";
        case SIDEBAR_TAB_MODULES:  return g_simple_mode ? "R" : "M";
        case SIDEBAR_TAB_STAGE:    return "S";
        default:                   return "?";
    }
}

static void sidebar_draw_section(int tab)
{
    switch (tab) {
        case SIDEBAR_TAB_OBJECTS:  sidebar_section_objects();  break;
        case SIDEBAR_TAB_IMAGES:   sidebar_section_images();   break;
        case SIDEBAR_TAB_PALETTES: sidebar_section_palettes(); break;
        case SIDEBAR_TAB_MODULES:  sidebar_section_modules();  break;
        case SIDEBAR_TAB_STAGE:    sidebar_section_stage();    break;
        default: break;
    }
}

/* --- frame ------------------------------------------------------------ */

/* Viewport actions and menus can ask for a specific editor; honour those before
   the tab bar is built so the request lands on this frame. */
static void sidebar_collect_requests(void)
{
    if (g_sidebar_restore) {
        g_sidebar_restore = false;
        right_sidebar_show_tab(g_sidebar_tab, -1);
    }

    /* Imports, tools and stage loads flip these flags to mean "show me that
       panel". Every section is always present now, so a flag switching on is
       read as a request to bring its tab forward. */
    static bool prev_images  = false;
    static bool prev_modules = false;
    static bool prev_layers  = false;
    static bool prev_minimap = false;
    static bool prev_init    = false;
    if (!prev_init) {
        prev_init = true;
        prev_images  = g_show_images;
        prev_modules = g_show_modules;
        prev_layers  = g_show_layers;
        prev_minimap = g_show_minimap;
    }
    if (g_show_images && !prev_images)   right_sidebar_show_tab(SIDEBAR_TAB_IMAGES, 0);
    if (g_show_modules && !prev_modules) right_sidebar_show_tab(SIDEBAR_TAB_MODULES, 0);
    if (g_show_layers && !prev_layers)   right_sidebar_show_tab(SIDEBAR_TAB_STAGE, 0);
    if (g_show_minimap && !prev_minimap) right_sidebar_show_tab(SIDEBAR_TAB_STAGE, 1);
    prev_images  = g_show_images;
    prev_modules = g_show_modules;
    prev_layers  = g_show_layers;
    prev_minimap = g_show_minimap;

    if (obj_properties_take_focus_request())
        right_sidebar_show_tab(SIDEBAR_TAB_OBJECTS, 0);
    if (g_runtime_binding_jump_module >= 0 && !g_simple_mode)
        right_sidebar_show_tab(SIDEBAR_TAB_MODULES, 3);
}

static void sidebar_draw_collapsed(float x, float top, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(SIDEBAR_COLLAPSED_W, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 4.0f));
    if (ImGui::Begin("##right_sidebar_collapsed", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::Button("<", ImVec2(-1.0f, 0.0f))) {
            g_sidebar_collapsed = false;
            g_sidebar_dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Expand the sidebar");
        ImGui::Separator();
        for (int t = 0; t < SIDEBAR_TAB_COUNT; t++) {
            ImGui::PushID(t);
            if (ImGui::Button(sidebar_tab_initial(t), ImVec2(-1.0f, 0.0f))) {
                g_sidebar_collapsed = false;
                g_sidebar_dirty = true;
                right_sidebar_show_tab(t, -1);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", sidebar_tab_label(t));
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* Left-edge drag handle. Runs before the panel is placed so a drag resizes the
   sidebar on the same frame the mouse moves. */
static void sidebar_draw_grip(float x, float top, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x - SIDEBAR_GRIP_W, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(SIDEBAR_GRIP_W, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##right_sidebar_grip", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground)) {
        ImGui::InvisibleButton("##grip", ImVec2(SIDEBAR_GRIP_W, h));
        bool hovered = ImGui::IsItemHovered();
        bool active  = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active) {
            float w = g_sidebar_width > 0.0f ? g_sidebar_width : sidebar_default_width();
            g_sidebar_width = sidebar_clamp_width(w - ImGui::GetIO().MouseDelta.x);
            g_sidebar_dirty = true;
        }
        ImU32 col = active  ? ImGui::GetColorU32(ImGuiCol_SeparatorActive)
                  : hovered ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered)
                            : ImGui::GetColorU32(ImGuiCol_Separator);
        ImVec2 p0 = ImGui::GetWindowPos();
        ImVec2 p1(p0.x + SIDEBAR_GRIP_W, p0.y + h);
        ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void draw_right_sidebar(void)
{
    sidebar_load();
    if (!right_sidebar_visible()) {
        g_sidebar_req_tab = -1;
        g_sidebar_req_sub = -1;
        palette_tools_frame_tick();
        return;
    }

    sidebar_collect_requests();

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float top = editor_canvas_top_y();
    float h = ds.y - top - status_bar_height();
    if (h < 160.0f) h = 160.0f;

    if (g_sidebar_collapsed) {
        sidebar_draw_collapsed(ds.x - SIDEBAR_COLLAPSED_W, top, h);
    } else {
        /* Resize first, then place the panel, so the drag has no visible lag. */
        float w = right_sidebar_width();
        sidebar_draw_grip(ds.x - w, top, h);
        w = right_sidebar_width();

        ImGui::SetNextWindowPos(ImVec2(ds.x - w, top), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        if (ImGui::Begin("##right_sidebar", NULL,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoSavedSettings)) {
            if (ImGui::Button(">")) {
                g_sidebar_collapsed = true;
                g_sidebar_dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Collapse the sidebar");
            ImGui::SameLine();

            if (ImGui::BeginTabBar("sidebar_sections",
                                   ImGuiTabBarFlags_FittingPolicyScroll |
                                   ImGuiTabBarFlags_TabListPopupButton)) {
                for (int t = 0; t < SIDEBAR_TAB_COUNT; t++) {
                    if (ImGui::BeginTabItem(sidebar_tab_label(t), NULL, sidebar_tab_flags(t))) {
                        if (g_sidebar_tab != t) {
                            g_sidebar_tab = t;
                            g_sidebar_dirty = true;
                        }
                        sidebar_draw_section(t);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    g_sidebar_req_tab = -1;
    g_sidebar_req_sub = -1;

    palette_tools_frame_tick();

    if (g_sidebar_dirty && !ImGui::GetIO().MouseDown[0]) {
        sidebar_save();
        g_sidebar_dirty = false;
    }
}
