#include "bg_editor_globals.h"

#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif
void json_write_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', f);
            fputc(*p, f);
        } else if (*p == '\n') {
            fputs("\\n", f);
        } else if (*p == '\r') {
            fputs("\\r", f);
        } else if (*p == '\t') {
            fputs("\\t", f);
        } else if (*p < 32) {
            fprintf(f, "\\u%04x", *p);
        } else {
            fputc(*p, f);
        }
    }
    fputc('"', f);
}

const char *stage_palette_mode_name(void)
{
    return (g_stage_bg_palette_mode == 0) ? "portal-core" : "grid";
}

const char *stage_overlay_mode_name(void)
{
    if (g_stage_overlay_mode == 1) return "spin";
    if (g_stage_overlay_mode == 2) return "inner-spin";
    return "pulse";
}

static void stage_basename_no_ext(const char *path, char *out, size_t outsz)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    snprintf(out, outsz, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static void stage_normalize_dir(char *path)
{
    if (!path) return;
    for (char *p = path; *p; p++)
        if (*p == '/') *p = '\\';
    size_t n = strlen(path);
    while (n > 1 && (path[n - 1] == '\\' || path[n - 1] == '/')) {
        path[n - 1] = '\0';
        n--;
    }
}

char *stage_read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > 1024 * 1024) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static const char *json_find_value(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char pattern[96];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

bool json_get_object_value(const char *json, const char *key, char *out, size_t outsz)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '{' || outsz < 2) return false;
    const char *start = p;
    bool in_string = false;
    bool escape = false;
    int depth = 0;
    for (; *p; ++p) {
        if (in_string) {
            if (escape) escape = false;
            else if (*p == '\\') escape = true;
            else if (*p == '"') in_string = false;
            continue;
        }
        if (*p == '"') {
            in_string = true;
            continue;
        }
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) {
                size_t n = (size_t)(p - start + 1);
                if (n >= outsz) n = outsz - 1;
                memcpy(out, start, n);
                out[n] = '\0';
                return true;
            }
        }
    }
    return false;
}

bool json_get_string_value(const char *json, const char *key, char *out, size_t outsz)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '"') return false;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < outsz) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[n++] = '\n';
            else if (*p == 'r') out[n++] = '\r';
            else if (*p == 't') out[n++] = '\t';
            else out[n++] = *p;
            p++;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
    return true;
}

bool json_get_int_value(const char *json, const char *key, int *out)
{
    const char *p = json_find_value(json, key);
    if (!p) return false;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    *out = (int)v;
    return true;
}

bool json_get_float_value(const char *json, const char *key, float *out)
{
    const char *p = json_find_value(json, key);
    if (!p) return false;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return false;
    *out = (float)v;
    return true;
}

bool json_get_bool_value(const char *json, const char *key, bool *out)
{
    const char *p = json_find_value(json, key);
    if (!p) return false;
    if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

bool stage_load_config(void)
{
    char *json = stage_read_text_file(g_stage_config_path);
    if (!json) {
        stage_set_toast("Could not read stage_config.json");
        return false;
    }
    g_stage_mirror_temple = true;

    char stage_dir_value[512];
    if (json_get_string_value(json, "stage_dir", stage_dir_value, sizeof stage_dir_value)) {
        if (strcmp(stage_dir_value, ".") == 0)
            stage_dirname(g_stage_config_path, g_stage_dir, sizeof g_stage_dir);
        else
            snprintf(g_stage_dir, sizeof g_stage_dir, "%s", stage_dir_value);
    }
    g_stage_auto_fit_sources = false;
    json_get_string_value(json, "name", g_stage_internal_name, sizeof g_stage_internal_name);
    json_get_string_value(json, "display_name", g_stage_display_name, sizeof g_stage_display_name);
    json_get_string_value(json, "bg_source", g_stage_bg_source, sizeof g_stage_bg_source);
    json_get_string_value(json, "mid_source", g_stage_mid_source, sizeof g_stage_mid_source);
    json_get_string_value(json, "floor_source", g_stage_floor_source, sizeof g_stage_floor_source);
    json_get_string_value(json, "rom_preview", g_stage_rom_preview, sizeof g_stage_rom_preview);
    json_get_string_value(json, "temple_scroll", g_stage_temple_scroll, sizeof g_stage_temple_scroll);
    json_get_string_value(json, "rear_fence_scroll", g_stage_rear_fence_scroll, sizeof g_stage_rear_fence_scroll);
    json_get_string_value(json, "front_rail_scroll", g_stage_front_rail_scroll, sizeof g_stage_front_rail_scroll);
    json_get_string_value(json, "floor_far_scroll", g_stage_floor_far_scroll, sizeof g_stage_floor_far_scroll);
    json_get_string_value(json, "floor_mid_scroll", g_stage_floor_mid_scroll, sizeof g_stage_floor_mid_scroll);
    json_get_string_value(json, "temple_main", g_stage_temple_main_source, sizeof g_stage_temple_main_source);
    json_get_string_value(json, "outer_rail", g_stage_outer_rail_source, sizeof g_stage_outer_rail_source);
    json_get_string_value(json, "rear_fence", g_stage_rear_fence_source, sizeof g_stage_rear_fence_source);
    json_get_string_value(json, "health_threshold", g_stage_health_threshold, sizeof g_stage_health_threshold);
    json_get_string_value(json, "comeback_margin", g_stage_comeback_margin, sizeof g_stage_comeback_margin);

    char bg_mode[64];
    if (json_get_string_value(json, "bg_mode", bg_mode, sizeof bg_mode))
        g_stage_bg_palette_mode = (strcmp(bg_mode, "grid") == 0) ? 1 : 0;

    json_get_int_value(json, "world_width", &g_stage_world_width);
    json_get_int_value(json, "bg_tile_w", &g_stage_bg_tile_w);
    json_get_int_value(json, "bg_tile_h", &g_stage_bg_tile_h);
    json_get_int_value(json, "floor_tile_w", &g_stage_floor_tile_w);
    json_get_int_value(json, "floor_tile_h", &g_stage_floor_tile_h);
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
    json_get_int_value(json, "visible_colors", &g_stage_visible_colors);
    json_get_int_value(json, "bg_cols", &g_stage_bg_cols);
    json_get_int_value(json, "bg_rows", &g_stage_bg_rows);
    json_get_int_value(json, "sleep_ticks", &g_stage_sleep_ticks);
    json_get_int_value(json, "steps", &g_stage_red_steps);
    json_get_int_value(json, "step_delay", &g_stage_red_step_delay);
    json_get_int_value(json, "fade_in_frames", &g_stage_red_fade_in_frames);
    json_get_int_value(json, "hold_frames", &g_stage_red_hold_frames);
    json_get_int_value(json, "fade_out_frames", &g_stage_red_fade_out_frames);
    json_get_int_value(json, "timer_threshold", &g_stage_red_timer_threshold);
    json_get_int_value(json, "temple_height", &g_stage_temple_height);
    json_get_int_value(json, "temple_y", &g_stage_temple_y);
    json_get_int_value(json, "outer_rail_height", &g_stage_outer_rail_height);
    json_get_int_value(json, "outer_rail_y", &g_stage_outer_rail_y);
    json_get_int_value(json, "rear_fence_height", &g_stage_rear_fence_height);
    json_get_int_value(json, "rear_fence_y", &g_stage_rear_fence_y);
    json_get_int_value(json, "far_end_y", &g_stage_floor_far_end_y);
    json_get_int_value(json, "mid_end_y", &g_stage_floor_mid_end_y);
    json_get_bool_value(json, "pan_mid", &g_stage_pan_mid);
    json_get_bool_value(json, "enabled", &g_stage_perspective_layers);
    json_get_bool_value(json, "enabled", &g_stage_floor_perspective);
    json_get_bool_value(json, "stock_portal_sides", &g_stage_stock_portal_sides);
    json_get_bool_value(json, "enabled", &g_stage_red_enabled);
    json_get_bool_value(json, "trigger_match_point", &g_stage_red_match_point);
    json_get_bool_value(json, "trigger_low_health", &g_stage_red_low_health);
    json_get_bool_value(json, "trigger_timer", &g_stage_red_timer);
    json_get_bool_value(json, "trigger_round_start", &g_stage_red_round_start);
    json_get_bool_value(json, "trigger_finish", &g_stage_red_finish);
    json_get_bool_value(json, "comeback_recover", &g_stage_red_comeback_recover);
    json_get_bool_value(json, "round3_only", &g_stage_red_round3_only);
    json_get_float_value(json, "background_strength", &g_stage_red_background_strength);
    json_get_float_value(json, "foreground_strength", &g_stage_red_foreground_strength);
    json_get_float_value(json, "arch_blue_strength", &g_stage_arch_blue_strength);

    char section[4096];
    if (json_get_object_value(json, "start_camera", section, sizeof section)) {
        json_get_bool_value(section, "enabled", &g_stage_start_camera_enabled);
        json_get_int_value(section, "worldx", &g_stage_start_camera_x);
        json_get_int_value(section, "worldy", &g_stage_start_camera_y);
        json_get_bool_value(section, "patch_bgnd", &g_stage_start_camera_patch_bgnd);
    }
    if (json_get_object_value(json, "perspective_layers", section, sizeof section)) {
        json_get_bool_value(section, "enabled", &g_stage_perspective_layers);
        json_get_bool_value(section, "mirror_temple", &g_stage_mirror_temple);
    }
    if (json_get_object_value(json, "floor_perspective", section, sizeof section)) {
        json_get_bool_value(section, "enabled", &g_stage_floor_perspective);
        json_get_int_value(section, "far_end_y", &g_stage_floor_far_end_y);
        json_get_int_value(section, "mid_end_y", &g_stage_floor_mid_end_y);
    }
    if (json_get_object_value(json, "build", section, sizeof section))
        json_get_bool_value(section, "auto_fit_sources", &g_stage_auto_fit_sources);

    free(json);
    stage_set_toast("Loaded stage_config.json");
    return true;
}

void stage_use_current_project(void)
{
    if (!g_bdb_path[0]) {
        stage_set_toast("Open or save a BDB first");
        return;
    }
    stage_dirname(g_bdb_path, g_stage_dir, sizeof g_stage_dir);
    stage_basename_no_ext(g_bdb_path, g_stage_internal_name, sizeof g_stage_internal_name);
    path_join(g_stage_config_path, sizeof g_stage_config_path, g_stage_dir, "stage_config.json");
    char preview_name[96];
    snprintf(preview_name, sizeof preview_name, "%s_rom_preview.png", g_stage_internal_name);
    snprintf(g_stage_rom_preview, sizeof g_stage_rom_preview, "%s", preview_name);
    stage_set_toast("Stage Kit now points at current BDB folder");
}

bool stage_write_config(void)
{
    char config_dir[512], stage_dir_value[512], stage_dir_cmp[512], config_dir_cmp[512];
    stage_dirname(g_stage_config_path, config_dir, sizeof config_dir);
    if (config_dir[0])
        ensure_dir_recursive(config_dir);

    FILE *f = fopen(g_stage_config_path, "w");
    if (!f) {
        stage_set_toast("Could not write stage_config.json");
        return false;
    }

    snprintf(stage_dir_value, sizeof stage_dir_value, "%s", g_stage_dir);
    snprintf(stage_dir_cmp, sizeof stage_dir_cmp, "%s", g_stage_dir);
    snprintf(config_dir_cmp, sizeof config_dir_cmp, "%s", config_dir);
    stage_normalize_dir(stage_dir_cmp);
    stage_normalize_dir(config_dir_cmp);
    if (strcasecmp(stage_dir_cmp, config_dir_cmp) == 0)
        snprintf(stage_dir_value, sizeof stage_dir_value, ".");

    fprintf(f, "{\n");
    fprintf(f, "  \"stage_dir\": "); json_write_string(f, stage_dir_value); fprintf(f, ",\n");
    fprintf(f, "  \"name\": "); json_write_string(f, g_stage_internal_name); fprintf(f, ",\n");
    fprintf(f, "  \"display_name\": "); json_write_string(f, g_stage_display_name); fprintf(f, ",\n");
    fprintf(f, "  \"mk2_stage_name_label\": \"txt_bg11\",\n");
    fprintf(f, "  \"bg_source\": "); json_write_string(f, g_stage_bg_source); fprintf(f, ",\n");
    fprintf(f, "  \"mid_source\": "); json_write_string(f, g_stage_mid_source); fprintf(f, ",\n");
    fprintf(f, "  \"floor_source\": "); json_write_string(f, g_stage_floor_source); fprintf(f, ",\n");
    fprintf(f, "  \"rom_preview\": "); json_write_string(f, g_stage_rom_preview); fprintf(f, ",\n");
    fprintf(f, "  \"build\": {\n");
    fprintf(f, "    \"world_width\": %d,\n", g_stage_world_width);
    fprintf(f, "    \"bg_fit_height\": %d,\n", g_stage_bg_fit_height);
    fprintf(f, "    \"pan_mid\": %s,\n", g_stage_pan_mid ? "true" : "false");
    fprintf(f, "    \"bg_tile_w\": %d,\n", g_stage_bg_tile_w);
    fprintf(f, "    \"bg_tile_h\": %d,\n", g_stage_bg_tile_h);
    fprintf(f, "    \"floor_tile_w\": %d,\n", g_stage_floor_tile_w);
    fprintf(f, "    \"floor_tile_h\": %d,\n", g_stage_floor_tile_h);
    fprintf(f, "    \"auto_fit_sources\": %s\n", g_stage_auto_fit_sources ? "true" : "false");
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
    fprintf(f, "  \"palette\": {\n");
    fprintf(f, "    \"visible_colors\": %d,\n", g_stage_visible_colors);
    fprintf(f, "    \"bg_mode\": "); json_write_string(f, stage_palette_mode_name()); fprintf(f, ",\n");
    fprintf(f, "    \"bg_cols\": %d,\n", g_stage_bg_cols);
    fprintf(f, "    \"bg_rows\": %d\n", g_stage_bg_rows);
    fprintf(f, "  },\n");
    fprintf(f, "  \"floor_perspective\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", g_stage_floor_perspective ? "true" : "false");
    fprintf(f, "    \"far_end_y\": %d,\n", g_stage_floor_far_end_y);
    fprintf(f, "    \"mid_end_y\": %d\n", g_stage_floor_mid_end_y);
    fprintf(f, "  },\n");
    fprintf(f, "  \"perspective_layers\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", g_stage_perspective_layers ? "true" : "false");
    fprintf(f, "    \"temple_main\": "); json_write_string(f, g_stage_temple_main_source); fprintf(f, ",\n");
    fprintf(f, "    \"mirror_temple\": %s,\n", g_stage_mirror_temple ? "true" : "false");
    fprintf(f, "    \"outer_rail\": "); json_write_string(f, g_stage_outer_rail_source); fprintf(f, ",\n");
    fprintf(f, "    \"rear_fence\": "); json_write_string(f, g_stage_rear_fence_source); fprintf(f, ",\n");
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
    fprintf(f, "    \"temple_scroll\": "); json_write_string(f, g_stage_temple_scroll); fprintf(f, ",\n");
    fprintf(f, "    \"rear_fence_scroll\": "); json_write_string(f, g_stage_rear_fence_scroll); fprintf(f, ",\n");
    fprintf(f, "    \"front_rail_scroll\": "); json_write_string(f, g_stage_front_rail_scroll); fprintf(f, ",\n");
    fprintf(f, "    \"floor_far_scroll\": "); json_write_string(f, g_stage_floor_far_scroll); fprintf(f, ",\n");
    fprintf(f, "    \"floor_mid_scroll\": "); json_write_string(f, g_stage_floor_mid_scroll); fprintf(f, ",\n");
    fprintf(f, "    \"stock_portal_sides\": %s,\n", g_stage_stock_portal_sides ? "true" : "false");
    fprintf(f, "    \"sleep_ticks\": %d\n", g_stage_sleep_ticks);
    fprintf(f, "  },\n");
    fprintf(f, "  \"start_camera\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", g_stage_start_camera_enabled ? "true" : "false");
    fprintf(f, "    \"worldx\": %d,\n", g_stage_start_camera_x);
    fprintf(f, "    \"worldy\": %d,\n", g_stage_start_camera_y);
    fprintf(f, "    \"patch_bgnd\": %s\n", g_stage_start_camera_patch_bgnd ? "true" : "false");
    fprintf(f, "  },\n");
    fprintf(f, "  \"red_shift\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", g_stage_red_enabled ? "true" : "false");
    fprintf(f, "    \"trigger\": \"danger\",\n");
    fprintf(f, "    \"trigger_match_point\": %s,\n", g_stage_red_match_point ? "true" : "false");
    fprintf(f, "    \"trigger_low_health\": %s,\n", g_stage_red_low_health ? "true" : "false");
    fprintf(f, "    \"trigger_timer\": %s,\n", g_stage_red_timer ? "true" : "false");
    fprintf(f, "    \"trigger_round_start\": %s,\n", g_stage_red_round_start ? "true" : "false");
    fprintf(f, "    \"trigger_finish\": %s,\n", g_stage_red_finish ? "true" : "false");
    fprintf(f, "    \"comeback_recover\": %s,\n", g_stage_red_comeback_recover ? "true" : "false");
    fprintf(f, "    \"round3_only\": %s,\n", g_stage_red_round3_only ? "true" : "false");
    fprintf(f, "    \"health_threshold\": "); json_write_string(f, g_stage_health_threshold); fprintf(f, ",\n");
    fprintf(f, "    \"comeback_margin\": "); json_write_string(f, g_stage_comeback_margin); fprintf(f, ",\n");
    fprintf(f, "    \"timer_threshold\": %d,\n", g_stage_red_timer_threshold);
    fprintf(f, "    \"steps\": %d,\n", g_stage_red_steps);
    fprintf(f, "    \"step_delay\": %d,\n", g_stage_red_step_delay);
    fprintf(f, "    \"fade_in_frames\": %d,\n", g_stage_red_fade_in_frames);
    fprintf(f, "    \"hold_frames\": %d,\n", g_stage_red_hold_frames);
    fprintf(f, "    \"fade_out_frames\": %d,\n", g_stage_red_fade_out_frames);
    fprintf(f, "    \"background_strength\": %.2f,\n", g_stage_red_background_strength);
    fprintf(f, "    \"foreground_strength\": %.2f\n", g_stage_red_foreground_strength);
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    fclose(f);
    stage_set_toast("Saved stage_config.json");
    return true;
}

void stage_append(char *out, size_t outsz, const char *text)
{
    size_t len = strlen(out);
    if (len >= outsz - 1) return;
    snprintf(out + len, outsz - len, "%s", text ? text : "");
}

void stage_append_arg(char *out, size_t outsz, const char *arg)
{
    stage_append(out, outsz, "\"");
    for (const char *p = arg ? arg : ""; *p; p++) {
        if (*p == '"')
            stage_append(out, outsz, "\\\"");
        else {
            char one[2] = {*p, '\0'};
            stage_append(out, outsz, one);
        }
    }
    stage_append(out, outsz, "\"");
}

int stage_effective_preview_worldx(void)
{
    return g_stage_start_camera_enabled ? g_stage_start_camera_x : g_stage_preview_worldx;
}

void stage_build_command(const char *action, char *out, size_t outsz)
{
    out[0] = '\0';
    stage_append_arg(out, outsz, "python");
    stage_append(out, outsz, " ");
    stage_append_arg(out, outsz, "tools\\mk2_stage_kit.py");
    stage_append(out, outsz, " --config ");
    stage_append_arg(out, outsz, g_stage_config_path);
    stage_append(out, outsz, " ");
    stage_append(out, outsz, action);
    if (strcmp(action, "rebuild") == 0 && g_stage_auto_fit_sources)
        stage_append(out, outsz, " --auto-prepare");

    if (strcmp(action, "promote") == 0 || strcmp(action, "package") == 0 ||
        strcmp(action, "rom-preview") == 0) {
        stage_append(out, outsz, " --mk2-root ");
        stage_append_arg(out, outsz, g_stage_mk2_root);
    }
    if (strcmp(action, "package") == 0) {
        stage_append(out, outsz, " --source-zip ");
        stage_append_arg(out, outsz, g_stage_source_zip);
        stage_append(out, outsz, " --worldx ");
        char nbuf[32];
        snprintf(nbuf, sizeof nbuf, "%d", stage_effective_preview_worldx());
        stage_append(out, outsz, nbuf);
        if (g_stage_no_install) stage_append(out, outsz, " --no-install");
    }
    if (strcmp(action, "rom-preview") == 0) {
        stage_append(out, outsz, " --worldx ");
        char nbuf[32];
        snprintf(nbuf, sizeof nbuf, "%d", stage_effective_preview_worldx());
        stage_append(out, outsz, nbuf);
    }
}

void stage_copy_command(const char *action)
{
    if (!stage_write_config()) return;
    stage_build_command(action, g_stage_last_command, sizeof g_stage_last_command);
    ImGui::SetClipboardText(g_stage_last_command);
    stage_set_toast("Copied Stage Kit command");
}

void stage_run_command(const char *action)
{
    if (!stage_write_config()) return;
    stage_build_command(action, g_stage_last_command, sizeof g_stage_last_command);
    /* The MK2 ROM assembly pipeline runs the external mk2-main toolchain, which
       is not bundled with this editor. The app never launches it itself; use
       "Copy Command" to run it manually where that toolchain is installed. */
    g_stage_last_rc = -1;
    stage_set_toast("MK2 ROM build needs the external mk2-main toolchain (not bundled). Use Copy Command.");
}

void stage_open_generated_bdb(void)
{
    char bdb[640], bdd[640], fname[96];
    snprintf(fname, sizeof fname, "%s.BDB", g_stage_internal_name);
    path_join(bdb, sizeof bdb, g_stage_dir, fname);
    snprintf(fname, sizeof fname, "%s.BDD", g_stage_internal_name);
    path_join(bdd, sizeof bdd, g_stage_dir, fname);
    if (!stage_file_exists(bdb) || !stage_file_exists(bdd)) {
        stage_set_toast("Rebuild first; BDB/BDD not found");
        return;
    }
    request_unsaved_action(UNSAVED_ACTION_OPEN_STAGE, bdb);
}

