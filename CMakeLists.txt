cmake_minimum_required(VERSION 3.16)

set(BDDVIEW_DEFAULT_VERSION "1.0.13")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BDDVIEW_DEFAULT_VERSION LIMIT_COUNT 1)
endif()
string(STRIP "${BDDVIEW_DEFAULT_VERSION}" BDDVIEW_DEFAULT_VERSION)

set(BDDVIEW_RELEASE_VERSION "${BDDVIEW_DEFAULT_VERSION}" CACHE STRING "bddview application version")
string(STRIP "${BDDVIEW_RELEASE_VERSION}" BDDVIEW_RELEASE_VERSION)
if(NOT BDDVIEW_RELEASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
    message(FATAL_ERROR "BDDVIEW_RELEASE_VERSION must be numeric semver, for example 1.0.2")
endif()

project(bddview VERSION "${BDDVIEW_RELEASE_VERSION}" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/Core")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/Core/app_version_resource.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Core/app_version_resource.h"
    @ONLY
)

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platform/bddview.rc")
    set(BDDVIEW_RC platform/bddview.rc)
endif()

add_executable(bddview
    bddview.cpp
    platform/bg_editor.cpp
    platform/Core/utils/app_diagnostics.cpp
    platform/Core/mk2_shared_paths.cpp
    platform/Core/utils/bdd_core.cpp
    platform/Core/utils/bdd_metadata.cpp
    platform/Core/bdd_metadata_editor.cpp
    platform/Core/utils/command_utils.cpp
    platform/Core/editor_bridge.cpp
    platform/Core/editor_commands.cpp
    platform/Core/editor_project_globals.cpp
    platform/Core/editor_project_storage.cpp
    platform/Core/utils/img_format.cpp
    platform/Core/image_lookup.cpp
    platform/Core/image_processing.cpp
    platform/Core/import_export.cpp
    platform/Core/mk2_analysis.cpp
    platform/Core/mk2_assembly_export.cpp
    platform/Core/project_header.cpp
    platform/Core/utils/path_utils.cpp
    platform/Core/project_snapshot.cpp
    platform/Core/recent_files.cpp
    platform/Core/stage_path_utils.cpp
    platform/libs/stb_impl.cpp
    platform/Core/test_projects.cpp
    platform/Core/tga_import.cpp
    platform/Core/viewer_test_levels.cpp
    platform/Core/viewer_geometry.cpp
    platform/Core/viewer_cli_commands.cpp
    platform/Core/viewer_load.cpp
    platform/Core/viewer_save.cpp
    platform/Core/viewer_stage_io.cpp
    platform/Core/world_module_utils.cpp
    platform/UI/overlays/alignment_doctor_overlay.cpp
    platform/UI/assets/app_icon.cpp
    platform/UI/app/autosave.cpp
    platform/UI/panels/block_editor.cpp
    platform/UI/dialogs/background_picker.cpp
    platform/UI/view/canvas_scrollbars.cpp
    platform/UI/panels/checkpoints_panel.cpp
    platform/UI/actions/clipboard.cpp
    platform/UI/panels/document_tabs.cpp
    platform/UI/panels/empty_canvas_hint.cpp
    platform/UI/app/editor_command_processor.cpp
    platform/UI/app/editor_events.cpp
    platform/UI/app/editor_lifecycle.cpp
    platform/UI/app/editor_render.cpp
    platform/UI/actions/export_viewport.cpp
    platform/UI/app/PanelManager.cpp
    platform/UI/app/settings.cpp
    platform/UI/app/stage_open.cpp
    platform/UI/panels/gc_panel.cpp
    platform/UI/view/game_view_controls.cpp
    platform/UI/overlays/game_view_overlay.cpp
    platform/UI/dialogs/grid_settings.cpp
    platform/UI/panels/group_bpp_reducer_panel.cpp
    platform/UI/assets/image_asset_helpers.cpp
    platform/UI/assets/image_draw_helpers.cpp
    platform/UI/dialogs/image_index_editor.cpp
    platform/UI/panels/image_list.cpp
    platform/UI/dialogs/info_dialogs.cpp
    platform/UI/dialogs/img_import_picker.cpp
    platform/UI/panels/layers_panel.cpp
    platform/UI/panels/menu_bar.cpp
    platform/UI/panels/minimap.cpp
    platform/UI/tools/mk2_asset_tools.cpp
    platform/UI/tools/mk2_authoring_tools.cpp
    platform/UI/tools/mk2_auto_repair_suggestions.cpp
    platform/UI/tools/mk2_budget_relief_suggestions.cpp
    platform/UI/tools/mk2_camera_bookmarks.cpp
    platform/UI/tools/mk2_danger_palette_designer.cpp
    platform/UI/tools/mk2_duplicate_mirror_finder.cpp
    platform/UI/tools/mk2_image_cleanup_tools.cpp
    platform/UI/tools/mk2_import_workspace_tools.cpp
    platform/UI/tools/mk2_integration_summary.cpp
    platform/UI/tools/mk2_layer_stack_inspector.cpp
    platform/UI/tools/mk2_layer_presets.cpp
    platform/UI/tools/mk2_level_start_helper_tool.cpp
    platform/UI/tools/mk2_load2_doctor_tool.cpp
    platform/UI/tools/mk2_lod_stale_warning.cpp
    platform/UI/tools/mk2_mirrored_asset_tool.cpp
    platform/UI/tools/mk2_one_click_validation_run.cpp
    platform/UI/tools/mk2_palette_animation_tools.cpp
    platform/UI/tools/mk2_palette_sync_prompt.cpp
    platform/UI/tools/mk2_palette_usage_optimizer.cpp
    platform/UI/tools/mk2_pan_coverage_scanner.cpp
    platform/UI/tools/mk2_project_seed_tools.cpp
    platform/UI/tools/mk2_simple_level_dialog.cpp
    platform/UI/tools/mk2_smart_palette_grouper.cpp
    platform/UI/tools/mk2_preview_diff_tool.cpp
    platform/UI/tools/mk2_quick_tile_tools.cpp
    platform/UI/tools/mk2_rom_space_reclaim.cpp
    platform/UI/tools/mk2_runtime_extras_backend.cpp
    platform/UI/tools/mk2_runtime_extras_overlay.cpp
    platform/UI/tools/mk2_runtime_extras_tool.cpp
    platform/UI/tools/mk2_runtime_lod_import.cpp
    platform/UI/tools/mk2_runtime_actor_tool.cpp
    platform/UI/tools/mk2_safe_dedup_assistant.cpp
    platform/UI/tools/mk2_selected_bpp_reducer_tool.cpp
    platform/UI/tools/mk2_stage_handoff_tools.cpp
    platform/UI/tools/mk2_stage_config.cpp
    platform/UI/tools/mk2_wedge_risk_tool.cpp
    platform/UI/tools/mk2_stage_recipe_manifest.cpp
    platform/UI/tools/mk2_stage_fx_builder_tool.cpp
    platform/UI/tools/mk2_stage_layer_role_editor.cpp
    platform/UI/tools/mk2_stage_preview_dashboard.cpp
    platform/UI/tools/mk2_stage_readiness_gate.cpp
    platform/UI/tools/mk2_stage_wiring_check.cpp
    platform/UI/tools/mk2_stage_repair_mode.cpp
    platform/UI/tools/mk2_stage_start_camera_tool.cpp
    platform/UI/tools/mk2_template_wizard_tool.cpp
    platform/UI/tools/mk2_transparency_advisor.cpp
    platform/UI/tools/mk2_visual_diagnostics.cpp
    platform/UI/tools/mk2_workflow.cpp
    platform/UI/tools/mk2_workspace_import.cpp
    platform/UI/overlays/module_bounds_overlay.cpp
    platform/UI/panels/modules_panel.cpp
    platform/UI/view/navigation.cpp
    platform/UI/dialogs/native_file_dialogs.cpp
    platform/UI/dialogs/new_project_dialog.cpp
    platform/UI/actions/object_actions.cpp
    platform/UI/panels/object_list.cpp
    platform/UI/actions/object_position_undo.cpp
    platform/UI/view/onboarding_hints.cpp
    platform/UI/panels/pal_anim_panel.cpp
    platform/UI/actions/palette_actions.cpp
    platform/UI/tools/palette_blend_merge_tool.cpp
    platform/UI/panels/palette_editor.cpp
    platform/UI/widgets/path_field.cpp
    platform/UI/dialogs/preferences_panel.cpp
    platform/UI/assets/preview_image_file.cpp
    platform/UI/panels/properties_panel.cpp
    platform/UI/dialogs/quit_dialog.cpp
    platform/UI/dialogs/reference_settings.cpp
    platform/UI/view/right_panel_layout.cpp
    platform/UI/panels/right_rail_panels.cpp
    platform/UI/dialogs/save_error_popup.cpp
    platform/UI/actions/selection_helpers.cpp
    platform/UI/actions/stage_command_clipboard.cpp
    platform/UI/app/save_project.cpp
    platform/UI/overlays/sdl_alignment_guides.cpp
    platform/UI/sdl/sdl_object_picker.cpp
    platform/UI/sdl/sdl_path_input.cpp
    platform/UI/overlays/sdl_reference_overlay.cpp
    platform/UI/sdl/sdl_save_popup.cpp
    platform/UI/sdl/sdl_stage_open.cpp
    platform/UI/sdl/sdl_tga_file_dialog.cpp
    platform/UI/sdl/sdl_selection_rect.cpp
    platform/UI/sdl/sdl_tooltip.cpp
    platform/UI/overlays/sdl_view_hud.cpp
    platform/UI/overlays/sdl_world_grid_overlay.cpp
    platform/UI/overlays/sdl_world_markers.cpp
    platform/UI/sdl/sdl_world_objects.cpp
    platform/UI/panels/split_object_dialog.cpp
    platform/UI/panels/sprite_resize_dialog.cpp
    platform/UI/tools/small_artifact_finder.cpp
    platform/UI/sdl/sdl_bitmap_font.cpp
    platform/UI/sdl/sdl_drop_file.cpp
    platform/UI/sdl/sdl_image_grid_view.cpp
    platform/UI/sdl/sdl_keyboard_actions.cpp
    platform/UI/sdl/sdl_mouse_interaction.cpp
    platform/UI/sdl/sdl_mouse_wheel.cpp
    platform/UI/sdl/sdl_object_drag_autopan.cpp
    platform/UI/view/stage_overview.cpp
    platform/UI/view/status_bar.cpp
    platform/UI/panels/stats_panels.cpp
    platform/UI/dialogs/tile_fill_dialog.cpp
    platform/UI/panels/tile_preview.cpp
    platform/UI/panels/toolbar.cpp
    platform/UI/assets/texture_cache.cpp
    platform/UI/view/toast_notifications.cpp
    platform/UI/panels/undo_history_panel.cpp
    platform/UI/panels/debug_info_panel.cpp
    platform/UI/panels/workspace_panels.cpp
    platform/UI/dialogs/unsaved_action_dialog.cpp
    platform/UI/dialogs/verify_dialog.cpp
    platform/UI/view/welcome_screen.cpp
    platform/UI/overlays/world_context_overlay.cpp
    platform/UI/overlays/world_game_rect_overlay.cpp
    platform/UI/overlays/world_object_overlays.cpp
    platform/UI/view/world_view_helpers.cpp
    platform/undo_manager.cpp
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_sdl2.cpp
    imgui/backends/imgui_impl_sdlrenderer2.cpp
    ${BDDVIEW_RC}
)

target_include_directories(bddview PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    platform
    imgui
    imgui/backends
)

target_compile_definitions(bddview PRIVATE BDDVIEW_VERSION="${PROJECT_VERSION}")

if(TARGET SDL2::SDL2)
    target_link_libraries(bddview PRIVATE SDL2::SDL2)
else()
    target_include_directories(bddview PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(bddview PRIVATE ${SDL2_LIBRARIES})
endif()

if(TARGET SDL2::SDL2main)
    target_link_libraries(bddview PRIVATE SDL2::SDL2main)
endif()

if(MSVC)
    target_compile_options(bddview PRIVATE /W3)
    target_compile_definitions(bddview PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
    set_target_properties(bddview PROPERTIES WIN32_EXECUTABLE TRUE)
    add_custom_command(TARGET bddview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:bddview>
            $<TARGET_FILE_DIR:bddview>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

if(NOT MSVC)
    target_compile_options(bddview PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Copy bundled runtime assets next to the exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platform/assets/MaterialSymbolsSharp-Regular.ttf")
    add_custom_command(TARGET bddview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:bddview>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/assets/MaterialSymbolsSharp-Regular.ttf"
            "$<TARGET_FILE_DIR:bddview>/assets/MaterialSymbolsSharp-Regular.ttf"
        VERBATIM
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/appicon.png")
    add_custom_command(TARGET bddview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:bddview>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/appicon.png"
            "$<TARGET_FILE_DIR:bddview>/assets/appicon.png"
        VERBATIM
    )
endif()

if(APPLE)
    set_target_properties(bddview PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

# ── bddtool: headless CLI (no SDL, no ImGui) ──────────────────────────
add_executable(bddtool platform/bddtool_cli.cpp platform/Core/utils/bdd_core.cpp)
target_include_directories(bddtool PRIVATE platform)
if(MSVC)
    target_compile_options(bddtool PRIVATE /W3)
    target_compile_definitions(bddtool PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(bddtool PRIVATE -Wall -Wextra)
endif()
