#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char *mk2_level_start_basename_ptr(const char *path)
{
    if (!path) return "";
    const char *a = std::strrchr(path, '\\');
    const char *b = std::strrchr(path, '/');
    const char *p = (a && (!b || a > b)) ? a : b;
    return p ? p + 1 : path;
}
static bool mk2_level_start_on_default_outer_haven_config(void)
{
    return strstr(g_stage_config_path, "bgprof_portal_movie_static_walk") != NULL;
}

static bool g_mk2_level_start_front_mirror_touched = false;

static void mk2_level_start_set_default_stage_folder(void)
{
    snprintf(g_stage_dir, sizeof g_stage_dir, "reference\\stages\\mk2_level_start");
    path_join(g_stage_config_path, sizeof g_stage_config_path, g_stage_dir, "stage_config.json");
    snprintf(g_stage_internal_name, sizeof g_stage_internal_name, "MK2START");
    snprintf(g_stage_display_name, sizeof g_stage_display_name, "CUSTOM STAGE");
    snprintf(g_stage_rom_preview, sizeof g_stage_rom_preview, "MK2START_rom_preview.png");
    g_stage_preview_worldx = (g_stage_world_width > 400) ? (g_stage_world_width - 400) / 2 : 0;
}

static void mk2_level_start_prepare_sources(bool protect_baseline_config)
{
    if (!g_stage_dir[0])
        snprintf(g_stage_dir, sizeof g_stage_dir, "reference\\stages\\mk2_level_start");
    if (protect_baseline_config && mk2_level_start_on_default_outer_haven_config())
        mk2_level_start_set_default_stage_folder();
    else if (protect_baseline_config)
        path_join(g_stage_config_path, sizeof g_stage_config_path, g_stage_dir, "stage_config.json");
    if (!g_stage_config_path[0])
        path_join(g_stage_config_path, sizeof g_stage_config_path, g_stage_dir, "stage_config.json");
    if (!g_stage_internal_name[0])
        snprintf(g_stage_internal_name, sizeof g_stage_internal_name, "MK2START");
    if (!g_stage_display_name[0])
        snprintf(g_stage_display_name, sizeof g_stage_display_name, "CUSTOM STAGE");
    if (!g_stage_rom_preview[0])
        snprintf(g_stage_rom_preview, sizeof g_stage_rom_preview, "%s_rom_preview.png", g_stage_internal_name);
    if (g_stage_world_width < 400)
        g_stage_world_width = 1203;

    if (g_stage_temple_main_source[0])
        snprintf(g_stage_mid_source, sizeof g_stage_mid_source, "%s", g_stage_temple_main_source);
    g_stage_auto_fit_sources = true;
    g_stage_pan_mid = true;
    g_stage_perspective_layers = true;
    g_stage_stock_portal_sides = false;
    if (!g_mk2_level_start_front_mirror_touched)
        g_stage_mirror_temple = false;
    g_stage_preview_worldx = (g_stage_world_width > 400) ? (g_stage_world_width - 400) / 2 : 0;
}

static void mk2_level_start_apply_defaults(void)
{
    mk2_level_start_set_default_stage_folder();
    g_stage_world_width = 1203;
    g_stage_bg_fit_height = 320;
    g_stage_bg_tile_w = 40;
    g_stage_bg_tile_h = 85;
    g_stage_floor_tile_w = 151;
    g_stage_floor_tile_h = 16;
    g_stage_floor_far_end_y = 192;
    g_stage_floor_mid_end_y = 224;
    g_stage_floor_perspective = true;
    g_stage_temple_height = 160;
    g_stage_temple_y = 32;
    g_stage_outer_rail_height = 192;
    g_stage_outer_rail_y = -2;
    g_stage_rear_fence_height = 1;
    g_stage_rear_fence_y = 0;
    g_stage_rear_fence_source[0] = '\0';
    g_stage_overlay_frames = 0;
    g_stage_overlay_streaks = 0;
    g_stage_overlay_strength = 0.0f;
    g_stage_bg_palette_mode = 1;
    g_stage_bg_cols = 4;
    g_stage_bg_rows = 2;
    g_stage_auto_fit_sources = true;
    g_stage_red_enabled = false;
    g_stage_arch_blue_strength = 0.0f;
    g_stage_mirror_temple = false;
    g_mk2_level_start_front_mirror_touched = false;
    snprintf(g_stage_temple_scroll, sizeof g_stage_temple_scroll, "0x18000");
    snprintf(g_stage_rear_fence_scroll, sizeof g_stage_rear_fence_scroll, "0x18000");
    snprintf(g_stage_front_rail_scroll, sizeof g_stage_front_rail_scroll, "0x20000");
    snprintf(g_stage_floor_far_scroll, sizeof g_stage_floor_far_scroll, "0x18000");
    snprintf(g_stage_floor_mid_scroll, sizeof g_stage_floor_mid_scroll, "0x1C000");
    mk2_level_start_prepare_sources(false);
    stage_set_toast("Level start defaults applied");
}

static bool mk2_level_start_file_ready(const char *path)
{
    char resolved[512];
    resolve_stage_file(resolved, sizeof resolved, path);
    return resolved[0] && stage_file_exists(resolved);
}

static void mk2_level_start_file_badge(const char *label, const char *path)
{
    bool ok = mk2_level_start_file_ready(path);
    ImGui::TextColored(ok ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f)
                          : ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                       "%s: %s", label, ok ? "OK" : "missing");
}

static void mk2_slot_pick_button(const char *id, const char *title, const char *role,
                                 char *path, size_t pathsz, const char *dialog_title)
{
    bool ok = mk2_level_start_file_ready(path);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, ok ? ImVec4(0.18f, 0.34f, 0.24f, 1.0f)
                                              : ImVec4(0.30f, 0.25f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ok ? ImVec4(0.24f, 0.44f, 0.30f, 1.0f)
                                                     : ImVec4(0.40f, 0.32f, 0.20f, 1.0f));
    ImVec2 slot_size(-1.0f, 58.0f);
    if (ImGui::Button("##slot", slot_size)) {
        char picked[512] = "";
        if (file_dialog_open(dialog_title, "PNG Files\0*.PNG;*.png\0All Files\0*.*\0", picked, sizeof picked))
            snprintf(path, pathsz, "%s", picked);
    }
    ImGui::PopStyleColor(2);

    ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(min.x + 8.0f, min.y + 6.0f), IM_COL32(235, 235, 245, 255), title);
    dl->AddText(ImVec2(min.x + 8.0f, min.y + 24.0f), IM_COL32(170, 180, 195, 230), role);
    const char *base = ok ? mk2_level_start_basename_ptr(path) : "click to choose PNG";
    dl->AddText(ImVec2(min.x + 8.0f, min.y + 40.0f), ok ? IM_COL32(160, 240, 175, 255)
                                                        : IM_COL32(255, 190, 100, 255), base);
    ImGui::PopID();
}

static void mk2_slot_fill_image(Img *im, int idx, const char *label, int w, int h, int fill, int accent)
{
    memset(im, 0, sizeof *im);
    im->idx = idx;
    im->w = w;
    im->h = h;
    im->pal_idx = 0;
    snprintf(im->label, sizeof im->label, "%s", label);
    snprintf(im->source, sizeof im->source, "%s", label);
    im->pix = (Uint8 *)malloc((size_t)w * (size_t)h);
    if (!im->pix) return;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int border = x < 3 || y < 3 || x >= w - 3 || y >= h - 3;
            int grid = (x % 32 == 0) || (y % 32 == 0);
            im->pix[y * w + x] = (Uint8)((border || grid) ? accent : fill);
        }
    }
}

static void mk2_slot_add_object(int *oi, int ii, int layer, int x, int y, int hfl)
{
    if (!oi) return;
    Obj *o = editor_project_append_object_slot();
    if (!o) return;
    o->wx = layer << 8;
    o->depth = x;
    o->sy = y;
    o->ii = ii;
    o->fl = 0;
    o->hfl = hfl;
    o->vfl = 0;
    o->order = *oi;
    (*oi)++;
}

static void create_mk2_slot_map_level(void)
{
    undo_save_ex("Create MK2 Slot Map");
    if (!editor_project_reserve_images(4) ||
        !editor_project_reserve_objects(9) ||
        !editor_project_reserve_modules(1) ||
        !editor_project_reserve_palettes(1)) {
        stage_set_toast("Could not allocate slot-map project storage");
        return;
    }
    editor_project_reset_loaded_stage();

    snprintf(g_name, sizeof g_name, "MK2START");
    snprintf(g_bdb_header, sizeof g_bdb_header, "MK2START 1203 320 255 1 1 9");
    g_have_bdb = 1;
    editor_project_set_single_module_line("START 0 1202 0 319");

    Uint32 slot_pal[256] = {};
    slot_pal[0] = 0x00000000u;
    slot_pal[1] = 0xFF2D5B9A; /* background */
    slot_pal[2] = 0xFF67A86A; /* floor */
    slot_pal[3] = 0xFFC05E5E; /* corner */
    slot_pal[4] = 0xFFE5C75A; /* front sprite */
    slot_pal[5] = 0xFF10121A; /* accent */
    slot_pal[6] = 0xFFB9C7DD;
    slot_pal[7] = 0xFFFFFFFF;
    editor_project_append_palette_slot("SLOTMAP", 8, slot_pal);
    g_sel_pal = 0;

    auto append_slot_image = [](int idx, const char *label, int w, int h, int fill, int accent) -> bool {
        Img *im = editor_project_append_image_slot();
        if (!im) return false;
        mk2_slot_fill_image(im, idx, label, w, h, fill, accent);
        return true;
    };
    if (!append_slot_image(0x10, "BG_PANEL_SLOT", 400, 160, 1, 5) ||
        !append_slot_image(0x11, "FLOOR_SLOT", 400, 80, 2, 5) ||
        !append_slot_image(0x12, "CORNER_SLOT", 96, 192, 3, 5) ||
        !append_slot_image(0x13, "FRONT_SPRITE_SLOT", 220, 96, 4, 5)) {
        stage_set_toast("Could not create slot-map images");
        return;
    }

    int oi = 0;
    mk2_slot_add_object(&oi, 0x10, 0x32, 0, 0, 0);
    mk2_slot_add_object(&oi, 0x10, 0x32, 400, 0, 0);
    mk2_slot_add_object(&oi, 0x10, 0x32, 800, 0, 0);
    mk2_slot_add_object(&oi, 0x11, 0x40, 0, 224, 0);
    mk2_slot_add_object(&oi, 0x11, 0x40, 400, 224, 0);
    mk2_slot_add_object(&oi, 0x11, 0x40, 800, 224, 0);
    mk2_slot_add_object(&oi, 0x12, 0x46, 0, 32, 0);
    mk2_slot_add_object(&oi, 0x12, 0x46, 1107, 32, 1);
    mk2_slot_add_object(&oi, 0x13, 0x46, 491, 128, 0);

    g_sel_flags[0] = 1;
    g_hl_obj = 0;
    snprintf(g_bdb_path, sizeof g_bdb_path, "MK2START.BDB");
    snprintf(g_bdd_path, sizeof g_bdd_path, "MK2START.BDD");
    sync_bdb_header_counts();
    g_tile_img = 0;
    g_tile_layer = 0x40;
    g_tile_cols = 1;
    g_tile_rows = 1;
    g_tile_sx = 400;
    g_tile_sy = 80;
    g_tile_ox = 0;
    g_tile_oy = 224;
    g_view_x = 401;
    g_view_y = 0;
    g_zoom = 1;
    g_cur_tool = 0;
    g_runtime_layout_view = 1;
    g_view_changed = 1;
    g_need_rebuild = 1;
    g_dirty = 1;
    g_show_images = true;
    g_show_mk2_workflow = true;
    snprintf(g_toast_msg, sizeof g_toast_msg, "Created MK2 editable slot map");
    g_toast_timer = 3.0f;
}

static int mk2_simple_import_png_slot(const char *path)
{
    int before = g_ni;
    import_png(path, false);
    if (g_ni <= before)
        return -1;
    return g_last_import_img;
}

static void mk2_simple_add_object(int *oi, int img_i, int layer, int x, int y, int hfl)
{
    if (!oi || img_i < 0 || img_i >= g_ni) return;
    Img *im = &g_img[img_i];
    Obj *o = editor_project_append_object_slot();
    if (!o) return;
    o->wx = (layer << 8) | (hfl ? 0x10 : 0);
    o->depth = x;
    o->sy = y;
    o->ii = im->idx;
    o->fl = (im->pal_idx >= 0) ? im->pal_idx : 0;
    o->hfl = hfl ? 1 : 0;
    o->order = *oi;
    (*oi)++;
}

bool create_mk2_simple_four_image_level(const char *bg_path,
                                        const char *floor_path,
                                        const char *corner_path,
                                        const char *front_path)
{
    if (!stage_file_exists(bg_path) ||
        !stage_file_exists(floor_path) ||
        !stage_file_exists(corner_path) ||
        !stage_file_exists(front_path)) {
        stage_set_toast("Pick all four PNG images first");
        return false;
    }

    undo_save_ex("Create Simple MK2 Level");
    if (!editor_project_reserve_images(4) ||
        !editor_project_reserve_objects(5) ||
        !editor_project_reserve_modules(1) ||
        !editor_project_reserve_palettes(4)) {
        stage_set_toast("Could not allocate simple-level project storage");
        return false;
    }
    editor_project_reset_loaded_stage();

    int bg_i = mk2_simple_import_png_slot(bg_path);
    int floor_i = mk2_simple_import_png_slot(floor_path);
    int corner_i = mk2_simple_import_png_slot(corner_path);
    int front_i = mk2_simple_import_png_slot(front_path);
    if (bg_i < 0 || floor_i < 0 || corner_i < 0 || front_i < 0) {
        stage_set_toast("One of the four PNGs could not be imported");
        return false;
    }

    Img *bg = &g_img[bg_i];
    Img *floor = &g_img[floor_i];
    Img *corner = &g_img[corner_i];
    Img *front = &g_img[front_i];
    int world_w = 1200;
    if (bg->w > world_w) world_w = bg->w;
    if (floor->w > world_w) world_w = floor->w;
    if (world_w < 400) world_w = 400;
    int floor_y = 224;
    int corner_y = 32;
    int front_y = 128;
    if (front->h > 0)
        front_y = 160 - front->h / 2;
    if (front_y < 0) front_y = 0;
    int world_h = 320;
    if (bg->h > world_h) world_h = bg->h;
    if (floor_y + floor->h > world_h) world_h = floor_y + floor->h;
    if (corner_y + corner->h > world_h) world_h = corner_y + corner->h;
    if (front_y + front->h > world_h) world_h = front_y + front->h;

    snprintf(g_name, sizeof g_name, "MK2SIMPLE");
    snprintf(g_bdb_header, sizeof g_bdb_header, "MK2SIMPLE %d %d 255 1 1 5", world_w, world_h);
    char module_line[256];
    snprintf(module_line, sizeof module_line, "SIMPLE 0 %d 0 %d", world_w - 1, world_h - 1);
    editor_project_set_single_module_line(module_line);
    g_have_bdb = 1;

    int oi = 0;
    mk2_simple_add_object(&oi, bg_i, 0x32, 0, 0, 0);
    mk2_simple_add_object(&oi, floor_i, 0x40, 0, floor_y, 0);
    mk2_simple_add_object(&oi, corner_i, 0x46, 0, corner_y, 0);
    mk2_simple_add_object(&oi, corner_i, 0x46, world_w - corner->w, corner_y, 1);
    mk2_simple_add_object(&oi, front_i, 0x46, (world_w - front->w) / 2, front_y, 0);
    if (g_no > 0) {
        g_sel_flags[0] = 1;
        g_hl_obj = 0;
    }

    snprintf(g_bdb_path, sizeof g_bdb_path, "MK2SIMPLE.BDB");
    snprintf(g_bdd_path, sizeof g_bdd_path, "MK2SIMPLE.BDD");
    sync_bdb_header_counts();
    g_simple_mode = true;
    g_game_view = 0;
    g_split_view = 0;
    g_runtime_layout_view = 1;
    g_cur_tool = 0;
    g_view_x = 0;
    g_view_y = 0;
    g_zoom = 1;
    g_view_changed = 1;
    g_need_rebuild = 1;
    g_dirty = 1;
    g_show_mk2_workflow = false;
    g_show_mk2_stage_kit = false;
    g_show_images = false;
    stage_set_toast("Created simple four-image MK2 level");
    return true;
}

void draw_mk2_level_start_helper_tool(void)
{
    ImGui::Text("MK2 Level Start Helper");
    if (ImGui::Button("Use Starter Defaults", ImVec2(-1, 0)))
        mk2_level_start_apply_defaults();
    if (ImGui::Button("Create Editable Slot Map", ImVec2(-1, 0)))
        create_mk2_slot_map_level();

    ImGui::Separator();
    ImGui::InputText("Stage Folder##starter", g_stage_dir, sizeof g_stage_dir);
    ImGui::SameLine();
    if (ImGui::SmallButton("...##starter_stage_dir")) {
        char path[512] = "";
        if (folder_dialog_open("Select Starter Stage Folder", path, sizeof path)) {
            snprintf(g_stage_dir, sizeof g_stage_dir, "%s", path);
            path_join(g_stage_config_path, sizeof g_stage_config_path, g_stage_dir, "stage_config.json");
        }
    }
    ImGui::InputText("LOAD2 Name##starter", g_stage_internal_name, sizeof g_stage_internal_name);
    ImGui::InputText("Display Name##starter", g_stage_display_name, sizeof g_stage_display_name);

    ImGui::Separator();
    if (ImGui::BeginTable("mk2_starter_slots", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mk2_slot_pick_button("bg", "Background", "wide rear art", g_stage_bg_source, sizeof g_stage_bg_source,
                             "Select MK2 Background PNG");
        ImGui::TableNextColumn();
        mk2_slot_pick_button("floor", "Floor", "walkable bottom", g_stage_floor_source, sizeof g_stage_floor_source,
                             "Select MK2 Floor PNG");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mk2_slot_pick_button("corner", "Corners", "one image mirrors", g_stage_outer_rail_source, sizeof g_stage_outer_rail_source,
                             "Select Corner PNG");
        ImGui::TableNextColumn();
        mk2_slot_pick_button("front", "Front Sprite", "in front of BG", g_stage_temple_main_source, sizeof g_stage_temple_main_source,
                             "Select Foreground Sprite PNG");
        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("File Paths##starter_paths")) {
        draw_path_field("Background PNG", g_stage_bg_source, sizeof g_stage_bg_source,
                        "Select MK2 Background PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0");
        draw_path_field("Floor PNG", g_stage_floor_source, sizeof g_stage_floor_source,
                        "Select MK2 Floor PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0");
        draw_path_field("Mirrored Corner PNG", g_stage_outer_rail_source, sizeof g_stage_outer_rail_source,
                        "Select Corner PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0");
        draw_path_field("Front Sprite PNG", g_stage_temple_main_source, sizeof g_stage_temple_main_source,
                        "Select Foreground Sprite PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0");
    }

    ImGui::Separator();
    bool ready = mk2_level_start_file_ready(g_stage_bg_source)
              && mk2_level_start_file_ready(g_stage_floor_source)
              && mk2_level_start_file_ready(g_stage_outer_rail_source)
              && mk2_level_start_file_ready(g_stage_temple_main_source);
    mk2_level_start_file_badge("Background", g_stage_bg_source);
    ImGui::SameLine();
    mk2_level_start_file_badge("Floor", g_stage_floor_source);
    mk2_level_start_file_badge("Corner", g_stage_outer_rail_source);
    ImGui::SameLine();
    mk2_level_start_file_badge("Front", g_stage_temple_main_source);

    if (!ready) ImGui::BeginDisabled();
    if (ImGui::Button("Generate Starter Stage", ImVec2(-1, 0))) {
        mk2_level_start_prepare_sources(true);
        stage_run_command("rebuild");
    }
    if (!ready) ImGui::EndDisabled();
    if (!ready)
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "Pick all four PNGs before rebuild.");

    if (ImGui::Button("Open Generated", ImVec2(-1, 0))) {
        mk2_level_start_prepare_sources(true);
        stage_open_generated_bdb();
    }

    if (ImGui::CollapsingHeader("Advanced Layout##starter")) {
        ImGui::InputInt("World Width##starter", &g_stage_world_width);
        ImGui::InputInt("BG Max Height##starter", &g_stage_bg_fit_height);
        ImGui::Checkbox("Floor Bands##starter", &g_stage_floor_perspective);
        ImGui::InputInt("Floor Far End Y##starter", &g_stage_floor_far_end_y);
        ImGui::InputInt("Floor Mid End Y##starter", &g_stage_floor_mid_end_y);
        ImGui::InputInt("Sprite Height##starter", &g_stage_temple_height);
        ImGui::InputInt("Sprite Y##starter", &g_stage_temple_y);
        ImGui::InputInt("Corner Height##starter", &g_stage_outer_rail_height);
        ImGui::InputInt("Corner Y##starter", &g_stage_outer_rail_y);
        ImGui::Checkbox("Auto-fit Sources##starter", &g_stage_auto_fit_sources);
        if (ImGui::Checkbox("Mirror Front Sprite##starter", &g_stage_mirror_temple))
            g_mk2_level_start_front_mirror_touched = true;
        ImGui::SliderFloat("Sprite Tint##starter", &g_stage_arch_blue_strength, 0.0f, 1.0f, "%.2f");

        ImGui::Separator();
        if (ImGui::SmallButton("Config = Stage Folder##starter_cfg"))
            path_join(g_stage_config_path, sizeof g_stage_config_path, g_stage_dir, "stage_config.json");
        ImGui::SameLine();
        if (ImGui::SmallButton("Save Config##starter")) {
            mk2_level_start_prepare_sources(true);
            stage_write_config();
        }
        if (ImGui::Button("Apply to Stage Kit", ImVec2(-1, 0))) {
            mk2_level_start_prepare_sources(true);
            g_show_mk2_stage_kit = true;
            stage_set_toast("Level start fields applied to Stage Kit");
        }
    }
}

