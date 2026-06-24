#include "bg_editor_globals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void stage_write_manifest_commands(FILE *f)
{
    if (!f) return;
    const char *actions[] = {"rebuild", "promote", "package", "rom-preview"};
    const char *labels[] = {"Rebuild BDB/BDD", "Promote to mk2-main", "Package ROM", "Render ROM preview"};
    for (int i = 0; i < 4; i++) {
        char cmd[2048];
        stage_build_command(actions[i], cmd, sizeof cmd);
        fprintf(f, "- **%s**: `%s`\n", labels[i], cmd);
    }
}

bool stage_write_package_manifest(void)
{
    char path[512];
    resolve_stage_file(path, sizeof path, g_stage_manifest_path);
    FILE *f = fopen(path, "w");
    if (!f) {
        stage_set_toast("Could not write package manifest");
        return false;
    }

    Mk2Diag d;
    mk2_collect_diag(&d);
    Mk2Budget b = mk2_collect_budget();
    PanCoverageSummary ps = mk2_compute_pan_summary();
    int hard = mk2_diag_hard_issues(&d);
    size_t dup = mk2_estimate_duplicate_savings();

    fprintf(f, "# MK2 Stage Package Manifest\n\n");
    fprintf(f, "## Stage\n\n");
    fprintf(f, "- Internal name: `%s`\n", g_stage_internal_name);
    fprintf(f, "- Display name: `%s`\n", g_stage_display_name);
    fprintf(f, "- Stage directory: `%s`\n", g_stage_dir);
    fprintf(f, "- Stage config: `%s`\n", g_stage_config_path);
    fprintf(f, "- BDB: `%s`\n", g_bdb_path[0] ? g_bdb_path : "(not saved)");
    fprintf(f, "- BDD: `%s`\n", g_bdd_path[0] ? g_bdd_path : "(not saved)");
    fprintf(f, "- ROM preview: `%s`\n\n", g_stage_rom_preview);

    fprintf(f, "## Sources\n\n");
    fprintf(f, "- Background: `%s`\n", g_stage_bg_source);
    fprintf(f, "- Mid/temple: `%s`\n", g_stage_mid_source);
    fprintf(f, "- Floor: `%s`\n\n", g_stage_floor_source);

    fprintf(f, "## Build Settings\n\n");
    fprintf(f, "- World width: `%d`\n", g_stage_world_width);
    fprintf(f, "- Preview world X: `%d`\n", g_stage_preview_worldx);
    fprintf(f, "- Stage start camera: `%s`, X `%d`, Y `%d`\n",
            g_stage_start_camera_enabled ? "enabled" : "disabled",
            g_stage_start_camera_x, g_stage_start_camera_y);
    fprintf(f, "- Fighter ground Y: `%s`, Y `%d`\n",
            g_stage_start_ground_enabled ? "enabled" : "disabled",
            g_stage_start_ground_y);
    fprintf(f, "- Palette mode: `%s`\n", stage_palette_mode_name());
    fprintf(f, "- Visible colors: `%d`\n", g_stage_visible_colors);
    fprintf(f, "- BG grid: `%d x %d`\n", g_stage_bg_cols, g_stage_bg_rows);
    fprintf(f, "- Pan mid layer: `%s`\n", g_stage_pan_mid ? "true" : "false");
    fprintf(f, "- Stock portal sides: `%s`\n", g_stage_stock_portal_sides ? "true" : "false");
    fprintf(f, "- Temple scroll: `%s`\n", g_stage_temple_scroll);
    fprintf(f, "- Overlay motion: `%s, %d frame(s), %d streaks/frame, strength %.2f, phase %.1f deg`\n\n",
            stage_overlay_mode_name(), g_stage_overlay_frames, g_stage_overlay_streaks,
            g_stage_overlay_strength, g_stage_overlay_phase_degrees);

    fprintf(f, "## Stage FX\n\n");
    fprintf(f, "- Red shift: `%s`\n", g_stage_red_enabled ? "enabled" : "disabled");
    fprintf(f, "- Triggers: `%s`\n", stage_fx_trigger_summary());
    fprintf(f, "- Health threshold: `%s`\n", g_stage_health_threshold);
    fprintf(f, "- Comeback margin: `%s`\n", g_stage_comeback_margin);
    fprintf(f, "- Timer threshold: `%d`\n", g_stage_red_timer_threshold);
    fprintf(f, "- Fade in / hold / fade out: `%d / %d / %d frames`\n",
            g_stage_red_fade_in_frames, g_stage_red_hold_frames, g_stage_red_fade_out_frames);
    fprintf(f, "- Background red strength: `%.2f`\n", g_stage_red_background_strength);
    fprintf(f, "- Temple/floor red strength: `%.2f`\n\n", g_stage_red_foreground_strength);

    fprintf(f, "## Layer Roles\n\n");
    fprintf(f, "| Layer | Role | Scroll | Red Strength | Objects |\n");
    fprintf(f, "|---:|---|---:|---:|---:|\n");
    int layer_count = mk2_layer_preset_count();
    for (int i = 0; i < layer_count; i++) {
        int layer = mk2_layer_preset_wx(i);
        int count = 0;
        for (int o = 0; o < g_no; o++) if (((g_obj[o].wx >> 8) & 0xFF) == layer) count++;
        fprintf(f, "| `0x%02X` | %s | %.2f | %.2f | %d |\n",
                layer, layer_role_name(g_layer_role[i]), mk2_scroll_factor_for_layer(layer),
                g_layer_role_tint[i], count);
    }
    fprintf(f, "\n");

    fprintf(f, "## Data Summary\n\n");
    fprintf(f, "- Objects: `%d`\n", g_no);
    fprintf(f, "- Images: `%d`\n", g_ni);
    fprintf(f, "- Palettes: `%d`\n", g_n_pals);
    fprintf(f, "- Modules: `%d`\n", g_bdb_num_modules);
    fprintf(f, "- LOAD2 caps: `%d/%d` palettes, `%d/%d` modules, `%d/%d` image headers\n",
            g_n_pals, MK2_LOAD2_MAX_STAGE_PALETTES,
            g_bdb_num_modules, MK2_LOAD2_MAX_MODULES,
            g_ni, MK2_LOAD2_MAX_IMAGE_HEADERS);
    fprintf(f, "- Max LOAD2 block estimate: `%d/%d` bytes at `%d` bpp\n",
            d.max_load2_block_bytes, MK2_LOAD2_MAX_DATA_BYTES, d.max_load2_block_bpp);
    fprintf(f, "- Runtime pressure: `%d/%d` sampled visible background objects at X `%d`, `%d` palette(s) used\n",
            d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP, d.max_visible_objects_x,
            d.runtime_palette_count);
    fprintf(f, "- Estimated payload: `0x%zX`\n", b.estimated_payload);
    fprintf(f, "- Raw image estimate: `0x%zX`\n", b.raw_image_bytes);
    fprintf(f, "- Largest image pixels: `%d`\n", b.max_image_pixels);
    fprintf(f, "- High-color images: `%d`\n", b.high_color_images);
    fprintf(f, "- Unused images: `%d`\n", b.unused_images);
    fprintf(f, "- Duplicate/mirror raw savings estimate: `0x%zX`\n\n", dup);

    if (g_stage_manifest_include_readiness) {
        char report[2048];
        mk2_readiness_report(report, sizeof report);
        fprintf(f, "## Readiness\n\n");
        fprintf(f, "```text\n%s```\n\n", report);
        fprintf(f, "- Hard issue count: `%d`\n", hard);
        fprintf(f, "- Pan coverage full/top/floor: `%.1f%% / %.1f%% / %.1f%%`\n", ps.full, ps.top, ps.floor);
        fprintf(f, "- Worst pan X: `%d`\n\n", ps.worst_x);
    }

    if (g_stage_manifest_include_bookmarks) {
        fprintf(f, "## Camera Bookmarks\n\n");
        fprintf(f, "| Name | X | Full | Top | Floor |\n");
        fprintf(f, "|---|---:|---:|---:|---:|\n");
        for (int i = 0; i < g_cam_bookmark_count; i++) {
            float full = mk2_screen_band_coverage(g_cam_bookmark_x[i], 0, g_pan_scan_view_h);
            float top = mk2_screen_band_coverage(g_cam_bookmark_x[i], 0, g_pan_scan_view_h / 5);
            float floor = mk2_screen_band_coverage(g_cam_bookmark_x[i], (g_pan_scan_view_h * 3) / 4, g_pan_scan_view_h);
            fprintf(f, "| %s | %d | %.1f%% | %.1f%% | %.1f%% |\n",
                    g_cam_bookmark_name[i], g_cam_bookmark_x[i], full, top, floor);
        }
        fprintf(f, "\n");
    }

    fprintf(f, "## Modules\n\n");
    if (g_bdb_num_modules == 0) {
        fprintf(f, "_No modules loaded._\n\n");
    } else {
        fprintf(f, "```text\n");
        for (int i = 0; i < g_bdb_num_modules; i++)
            fprintf(f, "%s\n", g_bdb_modules[i]);
        fprintf(f, "```\n\n");
    }

    if (g_stage_manifest_include_commands) {
        fprintf(f, "## Commands\n\n");
        stage_write_manifest_commands(f);
        fprintf(f, "\n");
    }

    fclose(f);
    stage_set_toast("Wrote package manifest");
    return true;
}

static void stage_write_recipe_string_field(FILE *f, const char *key, const char *value, bool comma)
{
    fprintf(f, "    \"%s\": ", key);
    json_write_string(f, value ? value : "");
    fprintf(f, "%s\n", comma ? "," : "");
}

static void stage_write_layer_roles_json(FILE *f, const char *indent)
{
    const char *sp = indent ? indent : "    ";
    int layer_count = mk2_layer_preset_count();
    for (int i = 0; i < layer_count; i++) {
        int layer = mk2_layer_preset_wx(i);
        fprintf(f, "%s{\"layer\": \"0x%02X\", \"role\": ", sp, layer);
        json_write_string(f, layer_role_name(g_layer_role[i]));
        fprintf(f, ", \"role_id\": %d, \"scroll\": %.2f, \"red_strength\": %.2f}%s\n",
                g_layer_role[i], mk2_scroll_factor_for_layer(layer), g_layer_role_tint[i],
                (i + 1 < layer_count) ? "," : "");
    }
}

static void stage_write_recipe_import_settings(FILE *f)
{
    fprintf(f, "  \"import_settings\": {\n");
    fprintf(f, "    \"gate_payload_limit\": %d,\n", g_gate_payload_limit);
    fprintf(f, "    \"gate_min_full_coverage\": %d,\n", g_gate_min_full_coverage);
    fprintf(f, "    \"gate_min_top_coverage\": %d,\n", g_gate_min_top_coverage);
    fprintf(f, "    \"gate_min_floor_coverage\": %d,\n", g_gate_min_floor_coverage);
    fprintf(f, "    \"gate_block_on_high_color\": %s,\n", g_gate_block_on_high_color ? "true" : "false");
    fprintf(f, "    \"pan_scan_start_x\": %d,\n", g_pan_scan_start_x);
    fprintf(f, "    \"pan_scan_end_x\": %d,\n", g_pan_scan_end_x);
    fprintf(f, "    \"pan_scan_step\": %d,\n", g_pan_scan_step);
    fprintf(f, "    \"pan_scan_stride\": %d,\n", g_pan_scan_stride);
    fprintf(f, "    \"pan_scan_view_w\": %d,\n", g_pan_scan_view_w);
    fprintf(f, "    \"pan_scan_view_h\": %d,\n", g_pan_scan_view_h);
    fprintf(f, "    \"validation_safe_fixes\": %s,\n", g_validation_safe_fixes ? "true" : "false");
    fprintf(f, "    \"validation_export_composite\": %s,\n", g_validation_export_composite ? "true" : "false");
    fprintf(f, "    \"validation_write_manifest\": %s,\n", g_validation_write_manifest ? "true" : "false");
    fprintf(f, "    \"validation_write_recipe\": %s,\n", g_validation_write_recipe ? "true" : "false");
    fprintf(f, "    \"bookmark_count\": %d,\n", g_cam_bookmark_count);
    for (int i = 0; i < 8; i++) {
        fprintf(f, "    \"bookmark_%d_name\": ", i);
        json_write_string(f, (i < g_cam_bookmark_count) ? g_cam_bookmark_name[i] : "");
        fprintf(f, ",\n");
        fprintf(f, "    \"bookmark_%d_x\": %d,\n", i, (i < g_cam_bookmark_count) ? g_cam_bookmark_x[i] : 0);
    }
    int layer_count = mk2_layer_preset_count();
    for (int i = 0; i < layer_count; i++) {
        int layer = mk2_layer_preset_wx(i);
        fprintf(f, "    \"layer_%02X_role\": %d,\n", layer, g_layer_role[i]);
        fprintf(f, "    \"layer_%02X_tint\": %.2f%s\n", layer, g_layer_role_tint[i],
                (i + 1 < layer_count) ? "," : "");
    }
    fprintf(f, "  },\n");
}

bool stage_write_patch_recipe(void)
{
    stage_write_config();
    char path[512];
    resolve_stage_file(path, sizeof path, g_stage_patch_recipe_path);
    FILE *f = fopen(path, "w");
    if (!f) {
        stage_set_toast("Could not write patch recipe");
        return false;
    }

    Mk2Diag d;
    mk2_collect_diag(&d);
    Mk2Budget b = mk2_collect_budget();
    PanCoverageSummary ps = mk2_compute_pan_summary();
    size_t dup = mk2_estimate_duplicate_savings();
    int hard = mk2_diag_hard_issues(&d);
    char rebuild_cmd[2048], promote_cmd[2048], package_cmd[2048], preview_cmd[2048];
    stage_build_command("rebuild", rebuild_cmd, sizeof rebuild_cmd);
    stage_build_command("promote", promote_cmd, sizeof promote_cmd);
    stage_build_command("package", package_cmd, sizeof package_cmd);
    stage_build_command("rom-preview", preview_cmd, sizeof preview_cmd);

    fprintf(f, "{\n");
    fprintf(f, "  \"schema\": \"midway-bddtool.mk2_patch_recipe.v1\",\n");
    fprintf(f, "  \"stage\": {\n");
    stage_write_recipe_string_field(f, "name", g_stage_internal_name, true);
    stage_write_recipe_string_field(f, "display_name", g_stage_display_name, true);
    fprintf(f, "    \"world_width\": %d,\n", g_stage_world_width);
    fprintf(f, "    \"bg_fit_height\": %d,\n", g_stage_bg_fit_height);
    fprintf(f, "    \"bg_tile_w\": %d,\n", g_stage_bg_tile_w);
    fprintf(f, "    \"bg_tile_h\": %d,\n", g_stage_bg_tile_h);
    fprintf(f, "    \"floor_tile_w\": %d,\n", g_stage_floor_tile_w);
    fprintf(f, "    \"floor_tile_h\": %d,\n", g_stage_floor_tile_h);
    fprintf(f, "    \"preview_worldx\": %d,\n", g_stage_preview_worldx);
    fprintf(f, "    \"start_camera_enabled\": %s,\n", g_stage_start_camera_enabled ? "true" : "false");
    fprintf(f, "    \"start_camera_x\": %d,\n", g_stage_start_camera_x);
    fprintf(f, "    \"start_camera_y\": %d,\n", g_stage_start_camera_y);
    fprintf(f, "    \"start_ground_enabled\": %s,\n", g_stage_start_ground_enabled ? "true" : "false");
    fprintf(f, "    \"start_ground_y\": %d,\n", g_stage_start_ground_y);
    fprintf(f, "    \"start_camera_patch_bgnd\": %s\n", g_stage_start_camera_patch_bgnd ? "true" : "false");
    fprintf(f, "  },\n");
    fprintf(f, "  \"files\": {\n");
    stage_write_recipe_string_field(f, "stage_dir", g_stage_dir, true);
    stage_write_recipe_string_field(f, "stage_config", g_stage_config_path, true);
    stage_write_recipe_string_field(f, "bdb", g_bdb_path, true);
    stage_write_recipe_string_field(f, "bdd", g_bdd_path, true);
    stage_write_recipe_string_field(f, "background", g_stage_bg_source, true);
    stage_write_recipe_string_field(f, "mid", g_stage_mid_source, true);
    stage_write_recipe_string_field(f, "floor", g_stage_floor_source, true);
    stage_write_recipe_string_field(f, "temple_main", g_stage_temple_main_source, true);
    stage_write_recipe_string_field(f, "outer_rail", g_stage_outer_rail_source, true);
    stage_write_recipe_string_field(f, "rear_fence", g_stage_rear_fence_source, true);
    stage_write_recipe_string_field(f, "rom_preview", g_stage_rom_preview, false);
    fprintf(f, "  },\n");
    fprintf(f, "  \"palette\": {\n");
    fprintf(f, "    \"visible_colors\": %d,\n", g_stage_visible_colors);
    fprintf(f, "    \"bg_mode\": "); json_write_string(f, stage_palette_mode_name()); fprintf(f, ",\n");
    fprintf(f, "    \"bg_cols\": %d,\n", g_stage_bg_cols);
    fprintf(f, "    \"bg_rows\": %d\n", g_stage_bg_rows);
    fprintf(f, "  },\n");
    fprintf(f, "  \"overlay\": {\n");
    fprintf(f, "    \"frames\": %d,\n", g_stage_overlay_frames);
    fprintf(f, "    \"streaks\": %d,\n", g_stage_overlay_streaks);
    fprintf(f, "    \"tile_w\": %d,\n", g_stage_overlay_tile_w);
    fprintf(f, "    \"tile_h\": %d,\n", g_stage_overlay_tile_h);
    fprintf(f, "    \"mode\": "); json_write_string(f, stage_overlay_mode_name()); fprintf(f, ",\n");
    fprintf(f, "    \"strength\": %.2f,\n", g_stage_overlay_strength);
    fprintf(f, "    \"line_width\": %.2f,\n", g_stage_overlay_line_width);
    fprintf(f, "    \"phase_degrees\": %.1f\n", g_stage_overlay_phase_degrees);
    fprintf(f, "  },\n");
    fprintf(f, "  \"perspective_layers\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", g_stage_perspective_layers ? "true" : "false");
    fprintf(f, "    \"temple_height\": %d,\n", g_stage_temple_height);
    fprintf(f, "    \"temple_y\": %d,\n", g_stage_temple_y);
    fprintf(f, "    \"outer_rail_height\": %d,\n", g_stage_outer_rail_height);
    fprintf(f, "    \"outer_rail_y\": %d,\n", g_stage_outer_rail_y);
    fprintf(f, "    \"rear_fence_height\": %d,\n", g_stage_rear_fence_height);
    fprintf(f, "    \"rear_fence_y\": %d,\n", g_stage_rear_fence_y);
    fprintf(f, "    \"arch_blue_strength\": %.2f\n", g_stage_arch_blue_strength);
    fprintf(f, "  },\n");
    fprintf(f, "  \"promote\": {\n");
    fprintf(f, "    \"pan_mid\": %s,\n", g_stage_pan_mid ? "true" : "false");
    fprintf(f, "    \"stock_portal_sides\": %s,\n", g_stage_stock_portal_sides ? "true" : "false");
    stage_write_recipe_string_field(f, "temple_scroll", g_stage_temple_scroll, true);
    stage_write_recipe_string_field(f, "rear_fence_scroll", g_stage_rear_fence_scroll, true);
    stage_write_recipe_string_field(f, "front_rail_scroll", g_stage_front_rail_scroll, true);
    stage_write_recipe_string_field(f, "floor_far_scroll", g_stage_floor_far_scroll, true);
    stage_write_recipe_string_field(f, "floor_mid_scroll", g_stage_floor_mid_scroll, true);
    fprintf(f, "    \"sleep_ticks\": %d\n", g_stage_sleep_ticks);
    fprintf(f, "  },\n");
    fprintf(f, "  \"red_shift\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", g_stage_red_enabled ? "true" : "false");
    fprintf(f, "    \"trigger_match_point\": %s,\n", g_stage_red_match_point ? "true" : "false");
    fprintf(f, "    \"trigger_low_health\": %s,\n", g_stage_red_low_health ? "true" : "false");
    fprintf(f, "    \"trigger_timer\": %s,\n", g_stage_red_timer ? "true" : "false");
    fprintf(f, "    \"trigger_round_start\": %s,\n", g_stage_red_round_start ? "true" : "false");
    fprintf(f, "    \"trigger_finish\": %s,\n", g_stage_red_finish ? "true" : "false");
    fprintf(f, "    \"comeback_recover\": %s,\n", g_stage_red_comeback_recover ? "true" : "false");
    fprintf(f, "    \"round3_only\": %s,\n", g_stage_red_round3_only ? "true" : "false");
    stage_write_recipe_string_field(f, "health_threshold", g_stage_health_threshold, true);
    stage_write_recipe_string_field(f, "comeback_margin", g_stage_comeback_margin, true);
    fprintf(f, "    \"timer_threshold\": %d,\n", g_stage_red_timer_threshold);
    fprintf(f, "    \"fade_in_frames\": %d,\n", g_stage_red_fade_in_frames);
    fprintf(f, "    \"hold_frames\": %d,\n", g_stage_red_hold_frames);
    fprintf(f, "    \"fade_out_frames\": %d,\n", g_stage_red_fade_out_frames);
    fprintf(f, "    \"background_strength\": %.2f,\n", g_stage_red_background_strength);
    fprintf(f, "    \"foreground_strength\": %.2f\n", g_stage_red_foreground_strength);
    fprintf(f, "  },\n");
    fprintf(f, "  \"readiness\": {\n");
    fprintf(f, "    \"hard_issues\": %d,\n", hard);
    fprintf(f, "    \"order_cautions\": %d,\n", d.order_issues);
    fprintf(f, "    \"load2_palettes\": %d,\n", g_n_pals);
    fprintf(f, "    \"load2_palette_limit\": %d,\n", MK2_LOAD2_MAX_STAGE_PALETTES);
    fprintf(f, "    \"load2_modules\": %d,\n", g_bdb_num_modules);
    fprintf(f, "    \"load2_module_limit\": %d,\n", MK2_LOAD2_MAX_MODULES);
    fprintf(f, "    \"load2_image_headers\": %d,\n", g_ni);
    fprintf(f, "    \"load2_image_header_limit\": %d,\n", MK2_LOAD2_MAX_IMAGE_HEADERS);
    fprintf(f, "    \"max_load2_block_bytes\": %d,\n", d.max_load2_block_bytes);
    fprintf(f, "    \"max_load2_block_bpp\": %d,\n", d.max_load2_block_bpp);
    fprintf(f, "    \"load2_block_byte_limit\": %d,\n", MK2_LOAD2_MAX_DATA_BYTES);
    fprintf(f, "    \"max_visible_background_objects\": %d,\n", d.max_visible_objects);
    fprintf(f, "    \"max_visible_background_objects_x\": %d,\n", d.max_visible_objects_x);
    fprintf(f, "    \"display_object_limit\": %d,\n", MK2_DISPLAY_OBJECT_CAP);
    fprintf(f, "    \"runtime_palettes_used\": %d,\n", d.runtime_palette_count);
    fprintf(f, "    \"runtime_palette_slot_limit\": %d,\n", MK2_RUNTIME_PALETTE_SLOTS);
    fprintf(f, "    \"payload_estimate\": %zu,\n", b.estimated_payload);
    fprintf(f, "    \"payload_limit\": %d,\n", g_gate_payload_limit);
    fprintf(f, "    \"high_color_images\": %d,\n", b.high_color_images);
    fprintf(f, "    \"duplicate_savings_estimate\": %zu,\n", dup);
    fprintf(f, "    \"pan_full\": %.2f,\n", ps.full);
    fprintf(f, "    \"pan_top\": %.2f,\n", ps.top);
    fprintf(f, "    \"pan_floor\": %.2f,\n", ps.floor);
    fprintf(f, "    \"worst_pan_x\": %d\n", ps.worst_x);
    fprintf(f, "  },\n");
    fprintf(f, "  \"camera_bookmarks\": [\n");
    for (int i = 0; i < g_cam_bookmark_count; i++) {
        fprintf(f, "    {\"name\": "); json_write_string(f, g_cam_bookmark_name[i]);
        fprintf(f, ", \"x\": %d}%s\n", g_cam_bookmark_x[i], (i + 1 < g_cam_bookmark_count) ? "," : "");
    }
    fprintf(f, "  ],\n");
    fprintf(f, "  \"layer_roles\": [\n");
    stage_write_layer_roles_json(f, "    ");
    fprintf(f, "  ],\n");
    stage_write_recipe_import_settings(f);
    fprintf(f, "  \"commands\": {\n");
    stage_write_recipe_string_field(f, "rebuild", rebuild_cmd, true);
    stage_write_recipe_string_field(f, "promote", promote_cmd, true);
    stage_write_recipe_string_field(f, "package", package_cmd, true);
    stage_write_recipe_string_field(f, "rom_preview", preview_cmd, false);
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    fclose(f);
    stage_set_toast("Wrote patch recipe");
    return true;
}

static void stage_import_roundtrip_settings(const char *json)
{
    char bg_mode[64];
    if (json_get_string_value(json, "bg_mode", bg_mode, sizeof bg_mode))
        g_stage_bg_palette_mode = (strcmp(bg_mode, "grid") == 0) ? 1 : 0;

    json_get_int_value(json, "sleep_ticks", &g_stage_sleep_ticks);
    json_get_bool_value(json, "enabled", &g_stage_red_enabled);
    json_get_bool_value(json, "trigger_match_point", &g_stage_red_match_point);
    json_get_bool_value(json, "trigger_low_health", &g_stage_red_low_health);
    json_get_bool_value(json, "trigger_timer", &g_stage_red_timer);
    json_get_bool_value(json, "trigger_round_start", &g_stage_red_round_start);
    json_get_bool_value(json, "trigger_finish", &g_stage_red_finish);
    json_get_bool_value(json, "comeback_recover", &g_stage_red_comeback_recover);
    json_get_bool_value(json, "round3_only", &g_stage_red_round3_only);
    json_get_string_value(json, "health_threshold", g_stage_health_threshold, sizeof g_stage_health_threshold);
    json_get_string_value(json, "comeback_margin", g_stage_comeback_margin, sizeof g_stage_comeback_margin);
    json_get_int_value(json, "timer_threshold", &g_stage_red_timer_threshold);
    json_get_int_value(json, "fade_in_frames", &g_stage_red_fade_in_frames);
    json_get_int_value(json, "hold_frames", &g_stage_red_hold_frames);
    json_get_int_value(json, "fade_out_frames", &g_stage_red_fade_out_frames);
    json_get_float_value(json, "background_strength", &g_stage_red_background_strength);
    json_get_float_value(json, "foreground_strength", &g_stage_red_foreground_strength);

    json_get_int_value(json, "gate_payload_limit", &g_gate_payload_limit);
    json_get_int_value(json, "gate_min_full_coverage", &g_gate_min_full_coverage);
    json_get_int_value(json, "gate_min_top_coverage", &g_gate_min_top_coverage);
    json_get_int_value(json, "gate_min_floor_coverage", &g_gate_min_floor_coverage);
    json_get_bool_value(json, "gate_block_on_high_color", &g_gate_block_on_high_color);
    json_get_int_value(json, "pan_scan_start_x", &g_pan_scan_start_x);
    json_get_int_value(json, "pan_scan_end_x", &g_pan_scan_end_x);
    json_get_int_value(json, "pan_scan_step", &g_pan_scan_step);
    json_get_int_value(json, "pan_scan_stride", &g_pan_scan_stride);
    json_get_int_value(json, "pan_scan_view_w", &g_pan_scan_view_w);
    json_get_int_value(json, "pan_scan_view_h", &g_pan_scan_view_h);
    json_get_bool_value(json, "validation_safe_fixes", &g_validation_safe_fixes);
    json_get_bool_value(json, "validation_export_composite", &g_validation_export_composite);
    json_get_bool_value(json, "validation_write_manifest", &g_validation_write_manifest);
    json_get_bool_value(json, "validation_write_recipe", &g_validation_write_recipe);
    json_get_int_value(json, "frames", &g_stage_overlay_frames);
    json_get_int_value(json, "streaks", &g_stage_overlay_streaks);
    json_get_int_value(json, "tile_w", &g_stage_overlay_tile_w);
    json_get_int_value(json, "tile_h", &g_stage_overlay_tile_h);
    char overlay_mode[64];
    if (json_get_string_value(json, "mode", overlay_mode, sizeof overlay_mode)) {
        if (strcmp(overlay_mode, "spin") == 0) g_stage_overlay_mode = 1;
        else if (strcmp(overlay_mode, "inner-spin") == 0) g_stage_overlay_mode = 2;
        else g_stage_overlay_mode = 0;
    }
    json_get_float_value(json, "strength", &g_stage_overlay_strength);
    json_get_float_value(json, "line_width", &g_stage_overlay_line_width);
    json_get_float_value(json, "phase_degrees", &g_stage_overlay_phase_degrees);

    int bookmark_count = g_cam_bookmark_count;
    if (json_get_int_value(json, "bookmark_count", &bookmark_count)) {
        if (bookmark_count < 0) bookmark_count = 0;
        if (bookmark_count > 8) bookmark_count = 8;
        g_cam_bookmark_count = bookmark_count;
        for (int i = 0; i < 8; i++) {
            char key[64];
            snprintf(key, sizeof key, "bookmark_%d_name", i);
            json_get_string_value(json, key, g_cam_bookmark_name[i], sizeof g_cam_bookmark_name[i]);
            snprintf(key, sizeof key, "bookmark_%d_x", i);
            json_get_int_value(json, key, &g_cam_bookmark_x[i]);
        }
    }

    int layer_count = mk2_layer_preset_count();
    for (int i = 0; i < layer_count; i++) {
        int layer = mk2_layer_preset_wx(i);
        char key[64];
        snprintf(key, sizeof key, "layer_%02X_role", layer);
        json_get_int_value(json, key, &g_layer_role[i]);
        if (g_layer_role[i] < 0) g_layer_role[i] = 0;
        if (g_layer_role[i] > 4) g_layer_role[i] = 4;
        snprintf(key, sizeof key, "layer_%02X_tint", layer);
        json_get_float_value(json, key, &g_layer_role_tint[i]);
        if (g_layer_role_tint[i] < 0.0f) g_layer_role_tint[i] = 0.0f;
        if (g_layer_role_tint[i] > 1.0f) g_layer_role_tint[i] = 1.0f;
    }
}

bool stage_import_patch_recipe(void)
{
    char path[512];
    resolve_stage_file(path, sizeof path, g_stage_patch_recipe_path);
    char *json = stage_read_text_file(path);
    if (!json) {
        stage_set_toast("Could not read patch recipe");
        return false;
    }
    json_get_string_value(json, "stage_config", g_stage_config_path, sizeof g_stage_config_path);
    if (stage_file_exists(g_stage_config_path)) stage_load_config();
    json_get_string_value(json, "name", g_stage_internal_name, sizeof g_stage_internal_name);
    json_get_string_value(json, "display_name", g_stage_display_name, sizeof g_stage_display_name);
    json_get_string_value(json, "stage_dir", g_stage_dir, sizeof g_stage_dir);
    json_get_string_value(json, "background", g_stage_bg_source, sizeof g_stage_bg_source);
    json_get_string_value(json, "mid", g_stage_mid_source, sizeof g_stage_mid_source);
    json_get_string_value(json, "floor", g_stage_floor_source, sizeof g_stage_floor_source);
    json_get_string_value(json, "rom_preview", g_stage_rom_preview, sizeof g_stage_rom_preview);
    json_get_int_value(json, "world_width", &g_stage_world_width);
    json_get_int_value(json, "bg_fit_height", &g_stage_bg_fit_height);
    json_get_int_value(json, "bg_tile_w", &g_stage_bg_tile_w);
    json_get_int_value(json, "bg_tile_h", &g_stage_bg_tile_h);
    json_get_int_value(json, "floor_tile_w", &g_stage_floor_tile_w);
    json_get_int_value(json, "floor_tile_h", &g_stage_floor_tile_h);
    json_get_int_value(json, "preview_worldx", &g_stage_preview_worldx);
    json_get_bool_value(json, "start_camera_enabled", &g_stage_start_camera_enabled);
    json_get_int_value(json, "start_camera_x", &g_stage_start_camera_x);
    json_get_int_value(json, "start_camera_y", &g_stage_start_camera_y);
    json_get_bool_value(json, "start_ground_enabled", &g_stage_start_ground_enabled);
    json_get_int_value(json, "start_ground_y", &g_stage_start_ground_y);
    json_get_bool_value(json, "start_camera_patch_bgnd", &g_stage_start_camera_patch_bgnd);
    json_get_int_value(json, "visible_colors", &g_stage_visible_colors);
    json_get_int_value(json, "bg_cols", &g_stage_bg_cols);
    json_get_int_value(json, "bg_rows", &g_stage_bg_rows);
    json_get_bool_value(json, "pan_mid", &g_stage_pan_mid);
    json_get_bool_value(json, "stock_portal_sides", &g_stage_stock_portal_sides);
    json_get_string_value(json, "temple_scroll", g_stage_temple_scroll, sizeof g_stage_temple_scroll);
    json_get_string_value(json, "rear_fence_scroll", g_stage_rear_fence_scroll, sizeof g_stage_rear_fence_scroll);
    json_get_string_value(json, "front_rail_scroll", g_stage_front_rail_scroll, sizeof g_stage_front_rail_scroll);
    json_get_string_value(json, "floor_far_scroll", g_stage_floor_far_scroll, sizeof g_stage_floor_far_scroll);
    json_get_string_value(json, "floor_mid_scroll", g_stage_floor_mid_scroll, sizeof g_stage_floor_mid_scroll);
    json_get_string_value(json, "temple_main", g_stage_temple_main_source, sizeof g_stage_temple_main_source);
    json_get_string_value(json, "outer_rail", g_stage_outer_rail_source, sizeof g_stage_outer_rail_source);
    json_get_string_value(json, "rear_fence", g_stage_rear_fence_source, sizeof g_stage_rear_fence_source);
    json_get_int_value(json, "temple_height", &g_stage_temple_height);
    json_get_int_value(json, "temple_y", &g_stage_temple_y);
    json_get_int_value(json, "outer_rail_height", &g_stage_outer_rail_height);
    json_get_int_value(json, "outer_rail_y", &g_stage_outer_rail_y);
    json_get_int_value(json, "rear_fence_height", &g_stage_rear_fence_height);
    json_get_int_value(json, "rear_fence_y", &g_stage_rear_fence_y);
    json_get_int_value(json, "far_end_y", &g_stage_floor_far_end_y);
    json_get_int_value(json, "mid_end_y", &g_stage_floor_mid_end_y);
    json_get_bool_value(json, "enabled", &g_stage_floor_perspective);
    json_get_float_value(json, "arch_blue_strength", &g_stage_arch_blue_strength);
    stage_import_roundtrip_settings(json);
    free(json);
    stage_set_toast("Imported patch recipe settings");
    return true;
}

