cmake_minimum_required(VERSION 3.16)
project(bddview VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platform/bddview.rc")
    set(BDDVIEW_RC platform/bddview.rc)
endif()

add_executable(bddview
    bddview.c
    platform/bg_editor.cpp
    platform/Core/app_diagnostics.cpp
    platform/Core/mk2_shared_paths.cpp
    platform/Core/autosave.cpp
    platform/Core/bdd_core.cpp
    platform/Core/bdd_metadata.cpp
    platform/Core/command_utils.cpp
    platform/Core/editor_bridge.cpp
    platform/Core/editor_project_globals.cpp
    platform/Core/editor_project_storage.cpp
    platform/Core/img_format.cpp
    platform/Core/image_lookup.cpp
    platform/Core/image_processing.cpp
    platform/Core/import_export.cpp
    platform/Core/mk2_analysis.cpp
    platform/Core/mk2_assembly_export.cpp
    platform/Core/path_utils.cpp
    platform/Core/project_snapshot.cpp
    platform/Core/recent_files.cpp
    platform/Core/runtime_lod_import.cpp
    platform/Core/save_project.cpp
    platform/Core/settings.cpp
    platform/Core/stage_config.cpp
    platform/Core/stage_open.cpp
    platform/Core/stage_recipe_manifest.cpp
    platform/Core/stb_impl.cpp
    platform/Core/test_projects.cpp
    platform/Core/tga_import.cpp
    platform/Core/viewer_test_levels.cpp
    platform/Core/viewer_geometry.cpp
    platform/Core/viewer_cli_commands.cpp
    platform/Core/viewer_load.cpp
    platform/Core/viewer_save.cpp
    platform/Core/viewer_stage_io.cpp
    platform/Core/workspace_import.cpp
    platform/Core/world_module_utils.cpp
    platform/UI/alignment_doctor_overlay.cpp
    platform/UI/app_icon.cpp
    platform/UI/block_editor.cpp
    platform/UI/background_picker.cpp
    platform/UI/canvas_scrollbars.cpp
    platform/UI/checkpoints_panel.cpp
    platform/UI/clipboard.cpp
    platform/UI/document_tabs.cpp
    platform/UI/empty_canvas_hint.cpp
    platform/UI/editor_events.cpp
    platform/UI/editor_lifecycle.cpp
    platform/UI/editor_render.cpp
    platform/UI/gc_panel.cpp
    platform/UI/game_view_controls.cpp
    platform/UI/game_view_overlay.cpp
    platform/UI/grid_settings.cpp
    platform/UI/group_bpp_reducer_panel.cpp
    platform/UI/image_asset_helpers.cpp
    platform/UI/image_draw_helpers.cpp
    platform/UI/image_index_editor.cpp
    platform/UI/image_list.cpp
    platform/UI/info_dialogs.cpp
    platform/UI/img_import_picker.cpp
    platform/UI/layers_panel.cpp
    platform/UI/menu_bar.cpp
    platform/UI/minimap.cpp
    platform/UI/mk2_asset_tools.cpp
    platform/UI/mk2_authoring_tools.cpp
    platform/UI/mk2_auto_repair_suggestions.cpp
    platform/UI/mk2_budget_relief_suggestions.cpp
    platform/UI/mk2_camera_bookmarks.cpp
    platform/UI/mk2_danger_palette_designer.cpp
    platform/UI/mk2_duplicate_mirror_finder.cpp
    platform/UI/mk2_image_cleanup_tools.cpp
    platform/UI/mk2_import_workspace_tools.cpp
    platform/UI/mk2_integration_summary.cpp
    platform/UI/mk2_layer_stack_inspector.cpp
    platform/UI/mk2_layer_presets.cpp
    platform/UI/mk2_level_start_helper_tool.cpp
    platform/UI/mk2_load2_doctor_tool.cpp
    platform/UI/mk2_mirrored_asset_tool.cpp
    platform/UI/mk2_one_click_validation_run.cpp
    platform/UI/mk2_palette_animation_tools.cpp
    platform/UI/mk2_palette_sync_prompt.cpp
    platform/UI/mk2_palette_usage_optimizer.cpp
    platform/UI/mk2_pan_coverage_scanner.cpp
    platform/UI/mk2_project_seed_tools.cpp
    platform/UI/mk2_simple_level_dialog.cpp
    platform/UI/mk2_smart_palette_grouper.cpp
    platform/UI/mk2_preview_diff_tool.cpp
    platform/UI/mk2_quick_tile_tools.cpp
    platform/UI/mk2_runtime_extras_backend.cpp
    platform/UI/mk2_runtime_extras_overlay.cpp
    platform/UI/mk2_runtime_extras_tool.cpp
    platform/UI/mk2_safe_dedup_assistant.cpp
    platform/UI/mk2_selected_bpp_reducer_tool.cpp
    platform/UI/mk2_stage_handoff_tools.cpp
    platform/UI/mk2_stage_fx_builder_tool.cpp
    platform/UI/mk2_stage_layer_role_editor.cpp
    platform/UI/mk2_stage_preview_dashboard.cpp
    platform/UI/mk2_stage_readiness_gate.cpp
    platform/UI/mk2_stage_repair_mode.cpp
    platform/UI/mk2_stage_start_camera_tool.cpp
    platform/UI/mk2_template_wizard_tool.cpp
    platform/UI/mk2_transparency_advisor.cpp
    platform/UI/mk2_visual_diagnostics.cpp
    platform/UI/mk2_workflow.cpp
    platform/UI/module_bounds_overlay.cpp
    platform/UI/modules_panel.cpp
    platform/UI/navigation.cpp
    platform/UI/new_project_dialog.cpp
    platform/UI/object_actions.cpp
    platform/UI/object_list.cpp
    platform/UI/object_position_undo.cpp
    platform/UI/onboarding_hints.cpp
    platform/UI/pal_anim_panel.cpp
    platform/UI/palette_actions.cpp
    platform/UI/palette_blend_merge_tool.cpp
    platform/UI/palette_editor.cpp
    platform/UI/path_field.cpp
    platform/UI/preferences_panel.cpp
    platform/UI/preview_image_file.cpp
    platform/UI/properties_panel.cpp
    platform/UI/quit_dialog.cpp
    platform/UI/reference_settings.cpp
    platform/UI/right_panel_layout.cpp
    platform/UI/save_error_popup.cpp
    platform/UI/selection_helpers.cpp
    platform/UI/sdl_alignment_guides.cpp
    platform/UI/sdl_object_picker.cpp
    platform/UI/sdl_path_input.cpp
    platform/UI/sdl_reference_overlay.cpp
    platform/UI/sdl_save_popup.cpp
    platform/UI/sdl_stage_open.cpp
    platform/UI/sdl_tga_file_dialog.cpp
    platform/UI/sdl_selection_rect.cpp
    platform/UI/sdl_tooltip.cpp
    platform/UI/sdl_view_hud.cpp
    platform/UI/sdl_world_grid_overlay.cpp
    platform/UI/sdl_world_markers.cpp
    platform/UI/sdl_world_objects.cpp
    platform/UI/split_object_dialog.cpp
    platform/UI/sprite_resize_dialog.cpp
    platform/UI/small_artifact_finder.cpp
    platform/UI/sdl_bitmap_font.cpp
    platform/UI/sdl_drop_file.cpp
    platform/UI/sdl_image_grid_view.cpp
    platform/UI/sdl_keyboard_actions.cpp
    platform/UI/sdl_mouse_interaction.cpp
    platform/UI/sdl_mouse_wheel.cpp
    platform/UI/sdl_object_drag_autopan.cpp
    platform/UI/stage_overview.cpp
    platform/UI/status_bar.cpp
    platform/UI/stats_panels.cpp
    platform/UI/tile_fill_dialog.cpp
    platform/UI/tile_preview.cpp
    platform/UI/toolbar.cpp
    platform/UI/texture_cache.cpp
    platform/UI/toast_notifications.cpp
    platform/UI/undo_history_panel.cpp
    platform/UI/unsaved_action_dialog.cpp
    platform/UI/verify_dialog.cpp
    platform/UI/welcome_screen.cpp
    platform/UI/world_context_overlay.cpp
    platform/UI/world_game_rect_overlay.cpp
    platform/UI/world_object_overlays.cpp
    platform/UI/world_view_helpers.cpp
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
add_executable(bddtool platform/bddtool_cli.cpp platform/Core/bdd_core.cpp)
target_include_directories(bddtool PRIVATE platform)
if(MSVC)
    target_compile_options(bddtool PRIVATE /W3)
    target_compile_definitions(bddtool PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(bddtool PRIVATE -Wall -Wextra)
endif()
