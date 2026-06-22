#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_commands.h"
#include "undo_manager.h"
#include "UI/app/PanelManager.h"
#include "UI/panels/DocumentTabsPanel.h"
#include "UI/panels/MenuBarPanel.h"
#include "UI/panels/RightRailPanels.h"
#include "UI/panels/StatsPanels.h"
#include "UI/panels/ToolbarPanel.h"
#include "UI/panels/ToolboxPanel.h"
#include "UI/panels/UtilityPanels.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <cstdio>
#include <cstdlib>
void bg_editor_init(SDL_Window *window, SDL_Renderer *renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    undo_manager_init();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "bddview_layout.ini";

    /* Delete old ini only on version change (bumped on major UI layout changes) */
    /* Currently version 6 - right rail uses dockable/reorderable packed panels */
    FILE *vi_f = fopen("bddview_layout.ver", "r");
    int layout_ver = 0;
    if (vi_f) { fscanf(vi_f, "%d", &layout_ver); fclose(vi_f); }
    if (layout_ver < 6) {
        remove("bddview_layout.ini");
        remove("bddview_right_panels.cfg");
        vi_f = fopen("bddview_layout.ver", "w");
        if (vi_f) { fprintf(vi_f, "6"); fclose(vi_f); }
    }

    settings_load();

    /* apply loaded prefs */
    io.FontGlobalScale = g_pref_font_scale;
    g_grid_sx = g_pref_grid_sx;
    g_grid_sy = g_pref_grid_sy;

    /* Try to load Material Symbols icon font for toolbar */
    {
        ImFontConfig cfg; cfg.MergeMode = true;
        ImFont *fnt = io.Fonts->AddFontDefault();
        FILE *tf = fopen("assets/MaterialSymbolsSharp-Regular.ttf", "rb");
        if (tf) { fclose(tf);
            static const ImWchar icons_ranges[] = { 0xE000, 0xF8FF, 0 };
            io.Fonts->AddFontFromFileTTF("assets/MaterialSymbolsSharp-Regular.ttf", 14.0f, &cfg, icons_ranges);
        }
        (void)fnt;
    }

    ImGui::StyleColorsDark();
    
    /* Adobe-like flat and rounded dark theme */
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding = 3.0f;
    
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.35f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.45f, 0.65f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    /* Unify the blue accent across interactive widgets so checkmarks, sliders,
       grips, selection and nav-highlight match the toolbar's highlight blue
       instead of falling back to ImGui's default brighter blue. */
    const ImVec4 accent     = ImVec4(0.22f, 0.48f, 0.85f, 1.00f);
    const ImVec4 accent_hov = ImVec4(0.28f, 0.56f, 0.95f, 1.00f);
    const ImVec4 accent_act = ImVec4(0.32f, 0.62f, 1.00f, 1.00f);
    colors[ImGuiCol_CheckMark]         = accent_act;
    colors[ImGuiCol_SliderGrab]        = accent;
    colors[ImGuiCol_SliderGrabActive]  = accent_act;
    colors[ImGuiCol_SeparatorHovered]  = accent;
    colors[ImGuiCol_SeparatorActive]   = accent_act;
    colors[ImGuiCol_ResizeGrip]        = ImVec4(0.30f, 0.30f, 0.34f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive]  = accent_act;
    colors[ImGuiCol_TextSelectedBg]    = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_NavHighlight]      = accent;
    colors[ImGuiCol_DragDropTarget]    = accent_act;
    (void)accent_hov;

    /* Fill in the remaining window chrome so popups, menus and titles stay on
       the same flat-dark palette rather than ImGui defaults. */
    colors[ImGuiCol_TitleBg]          = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_PopupBg]          = ImVec4(0.13f, 0.13f, 0.13f, 0.98f);
    colors[ImGuiCol_MenuBarBg]        = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_Separator]        = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.10f, 0.10f, 0.10f, 0.40f);

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    g_panel_manager.clear();
    g_panel_manager.register_panel(std::make_unique<MenuBarPanel>());
    g_panel_manager.register_panel(std::make_unique<ToolbarPanel>());
    g_panel_manager.register_panel(std::make_unique<ToolboxPanel>());
    g_panel_manager.register_panel(std::make_unique<DocumentTabsPanel>());
    g_panel_manager.register_panel(std::make_unique<TilePreviewPanel>());
    g_panel_manager.register_panel(std::make_unique<EmptyCanvasHintPanel>());
    g_panel_manager.register_panel(std::make_unique<UndoHistoryPanel>());
    g_panel_manager.register_panel(std::make_unique<DebugInfoPanel>());
    g_panel_manager.register_panel(std::make_unique<PaletteAnimationPanel>());
    g_panel_manager.register_panel(std::make_unique<LevelStatsPanel>());
    g_panel_manager.register_panel(std::make_unique<BppPreviewPanel>());
    g_panel_manager.register_panel(std::make_unique<GarbageCollectPanel>());
    g_panel_manager.register_panel(std::make_unique<CheckpointsPanel>());
    g_panel_manager.register_panel(std::make_unique<GroupBppReducerPanel>());
    g_panel_manager.register_panel(std::make_unique<ObjectListPanel>());
    g_panel_manager.register_panel(std::make_unique<ObjectPropertiesPanel>());
    g_panel_manager.register_panel(std::make_unique<BlockEditorPanel>());
    g_panel_manager.register_panel(std::make_unique<SpriteResizePanel>());
    g_panel_manager.register_panel(std::make_unique<SplitObjectPanel>());
    g_panel_manager.register_panel(std::make_unique<PalettePanel>());
    g_panel_manager.register_panel(std::make_unique<ModulesPanel>());
    g_panel_manager.register_panel(std::make_unique<LayersPanel>());
    g_panel_manager.register_panel(std::make_unique<ImageListPanel>());
    g_panel_manager.register_panel(std::make_unique<MinimapPanel>());

    recent_load();
}


void bg_editor_new_frame(void)
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

float editor_canvas_top_y(void)
{
    if (g_preview_mode)
        return 0.0f;
    float menu_h = ImGui::GetFrameHeight();
    float top = menu_h + 38.0f;   /* toolbar strip */
    if (g_num_docs >= 1)
        top += 30.0f;             /* document tab / info strip */
    return top;                   /* canvas grid starts flush below the strips */
}

int bg_editor_canvas_top_px(void)
{
    return (int)(editor_canvas_top_y() + 0.5f);
}


void bg_editor_shutdown(void)
{
    editor_clear_commands();
    g_panel_manager.clear();
    undo_manager_shutdown();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

int bg_editor_request_close(void)
{
    if (has_unsaved_work()) {
        request_unsaved_action(UNSAVED_ACTION_CLOSE_APP);
        return 0;
    }
    return 1;
}

int bg_editor_take_close_approved(void)
{
    if (!g_close_approved)
        return 0;
    g_close_approved = false;
    return 1;
}

int bg_editor_wants_input(void)
{
    return (ImGui::GetIO().WantCaptureMouse ||
            g_runtime_guide_mouse_capture ||
            g_canvas_scrollbar_mouse_capture) ? 1 : 0;
}

int bg_editor_wants_wheel(void)
{
    if (g_runtime_guide_mouse_capture || g_canvas_scrollbar_mouse_capture)
        return 1;
    if (ImGui::IsAnyItemActive())
        return 1;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        return 1;
    return 0;
}

int bg_editor_wants_keyboard(void)
{
    return ImGui::GetIO().WantCaptureKeyboard ? 1 : 0;
}


