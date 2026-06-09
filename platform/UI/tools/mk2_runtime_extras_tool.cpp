#include "bg_editor_globals.h"
#include "undo_manager.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define mk2_runtime_strcasecmp _stricmp
#else
#include <strings.h>
#define mk2_runtime_strcasecmp strcasecmp
#endif
#define strcasecmp mk2_runtime_strcasecmp
static bool mk2_current_stage_is_tower2(void)
{
    if (g_name[0] && strcasecmp(g_name, "TOWER2") == 0) return true;
    if (g_stage_internal_name[0] && strcasecmp(g_stage_internal_name, "TOWER2") == 0) return true;
    return strstr(g_bdb_path, "TOWER2") || strstr(g_bdb_path, "tower2") ||
           strstr(g_bdd_path, "TOWER2") || strstr(g_bdd_path, "tower2");
}

bool mk2_current_stage_is_battle(void)
{
    if (g_name[0] && strcasecmp(g_name, "BATTLE") == 0) return true;
    if (g_stage_internal_name[0] && strcasecmp(g_stage_internal_name, "BATTLE") == 0) return true;
    return strstr(g_bdb_path, "BATTLE") || strstr(g_bdb_path, "battle") ||
           strstr(g_bdd_path, "BATTLE") || strstr(g_bdd_path, "battle");
}

static bool mk2_current_stage_is_forest(void)
{
    if (g_name[0] && (strcasecmp(g_name, "FOREST") == 0 || strcasecmp(g_name, "FOREST2") == 0)) return true;
    if (g_stage_internal_name[0] &&
        (strcasecmp(g_stage_internal_name, "FOREST") == 0 || strcasecmp(g_stage_internal_name, "FOREST2") == 0))
        return true;
    return strstr(g_bdb_path, "FOREST") || strstr(g_bdb_path, "forest") ||
           strstr(g_bdd_path, "FOREST") || strstr(g_bdd_path, "forest");
}

static bool mk2_current_stage_looks_like_tower_runtime(void)
{
    if (mk2_current_stage_is_tower2()) return true;
    if (g_name[0] && (strcasecmp(g_name, "TWGCLOUD") == 0 || strcasecmp(g_name, "TWGCLOUDS") == 0)) return true;
    if (g_stage_internal_name[0] &&
        (strcasecmp(g_stage_internal_name, "TWGCLOUD") == 0 || strcasecmp(g_stage_internal_name, "TWGCLOUDS") == 0))
        return true;
    return strstr(g_bdb_path, "TWGCLOUD") || strstr(g_bdb_path, "twgcloud") ||
           strstr(g_bdd_path, "TWGCLOUD") || strstr(g_bdd_path, "twgcloud");
}

static int mk2_runtime_suggested_preset_for_stage(void)
{
    if (mk2_current_stage_looks_like_tower_runtime()) return 1;
    if (mk2_current_stage_is_battle()) return 2;
    if (mk2_current_stage_is_forest()) return 4;
    return 0;
}

static int mk2_runtime_stage_kind(void)
{
    return g_runtime_recipe_kind;
}

static void runtime_guides_load_preset(int kind);

bool mk2_current_stage_has_known_runtime_extras(void)
{
    return g_runtime_recipe_kind != 0;
}

const RuntimeExtraGuide g_tower_runtime_guide_defaults[] = {
    { "STATUE2", "STATUE2 rear L", "CASTLEP.LOD", 316, -4, 68, 195, 0.6875f, 0x43, 0, IM_COL32(210, 125, 70, 130) },
    { "STATUE1", "STATUE1 front L", "CASTLEP.LOD", 284, -12, 76, 218, 0.75f, 0x46, 0, IM_COL32(235, 145, 80, 145) },
    { "STATUE2", "STATUE2 rear R", "CASTLEP.LOD", 768, -4, 68, 195, 0.6875f, 0x43, 1, IM_COL32(210, 125, 70, 130) },
    { "STATUE1", "STATUE1 front R", "CASTLEP.LOD", 804, -12, 76, 218, 0.75f, 0x46, 1, IM_COL32(235, 145, 80, 145) },
    { "FlameA1", "Flame L", "MK6MIL.LOD", 272, -5, 36, 48, 0.75f, 0x46, 0, IM_COL32(255, 90, 40, 135) },
    { "FlameA1", "Flame R", "MK6MIL.LOD", 822, -5, 36, 48, 0.75f, 0x46, 1, IM_COL32(255, 90, 40, 135) },
    { "cloud1a", "Cloud row 7", "MK6MIL.LOD", 410, 42, 370, 145, 0.625f, 0x32, 0, IM_COL32(140, 175, 220, 90) },
    { "cloud1a", "Cloud row 8", "MK6MIL.LOD", 458, 0, 370, 145, 0.625f, 0x32, 0, IM_COL32(150, 185, 235, 90) },
    { "FL_TOW", "FL_TOW floor", "MK7MIL.LOD", 0, 181, 664, 75, 1.0f, 0x40, 0, IM_COL32(110, 210, 125, 95) },
    { "monktorso", "Big monk", "MK6MIL.LOD", 536, 72, 130, 160, 0.625f, 0x3C, 0, IM_COL32(185, 135, 230, 95) },
};
const int g_tower_runtime_guide_defaults_count =
    (int)(sizeof(g_tower_runtime_guide_defaults) / sizeof(g_tower_runtime_guide_defaults[0]));

const RuntimeExtraGuide g_battle_runtime_guide_defaults[] = {
    { "FL_BATTL", "FL_BATTL floor", "MK7MIL.LOD", 0, 210, 664, 45, 1.0f, 0x40, 0, IM_COL32(110, 210, 125, 95) },
    { "RUBLE1", "Rubble pile L", "BATTLE.IMG / MK7MIL.LOD", 92, 108, 116, 83, 0.5f, 0x43, 0, IM_COL32(190, 155, 110, 115) },
    { "BURN_vda", "Burning debris", "BATTLE.IMG / MK7MIL.LOD", 260, 112, 91, 64, 0.5f, 0x43, 0, IM_COL32(255, 110, 55, 130) },
    { "skulls", "Skull pile", "BATTLE.IMG / MK7MIL.LOD", 390, 112, 102, 59, 0.5f, 0x43, 0, IM_COL32(220, 210, 180, 115) },
    { "SKELTS", "Skeleton pile", "BATTLE.IMG / MK7MIL.LOD", 560, 105, 157, 72, 0.5f, 0x43, 0, IM_COL32(205, 205, 225, 115) },
    { "ROCK_vda", "Rock formation", "BATTLE.IMG / MK7MIL.LOD", 672, 118, 196, 56, 0.5f, 0x43, 0, IM_COL32(140, 145, 150, 115) },
    { "BURN6_vda", "Chain/hook fire", "BATTLE.IMG / MK7MIL.LOD", 970, 7, 124, 91, 0.5f, 0x45, 0, IM_COL32(255, 85, 40, 130) },
    { "RUBLE2", "Rubble pile R", "BATTLE.IMG / MK7MIL.LOD", 992, 108, 165, 83, 0.5f, 0x43, 0, IM_COL32(190, 155, 110, 115) },
};
const int g_battle_runtime_guide_defaults_count =
    (int)(sizeof(g_battle_runtime_guide_defaults) / sizeof(g_battle_runtime_guide_defaults[0]));

const RuntimeExtraGuide g_forest_runtime_guide_defaults[] = {
    { "FL_FORST", "FL_FORST floor", "MK7MIL.LOD", 0, 0xD7, 664, 39, 1.0f, 0x40, 0, IM_COL32(110, 210, 125, 95) },
};
const int g_forest_runtime_guide_defaults_count =
    (int)(sizeof(g_forest_runtime_guide_defaults) / sizeof(g_forest_runtime_guide_defaults[0]));

RuntimeExtraGuide g_tower_runtime_guides[MAX_RUNTIME_EXTRA_GUIDES];
bool g_tower_runtime_guides_init = false;
bool g_tower_runtime_guides_dirty = false;
int  g_tower_runtime_selected = 0;
int  g_tower_runtime_stage_kind = 0;
int  g_tower_runtime_guide_n = 0;

int tower_runtime_guide_count(void)
{
    return g_tower_runtime_guide_n;
}

static const RuntimeExtraGuide *runtime_guide_defaults_for_stage(int kind, int *count)
{
    if (count) *count = 0;
    if (kind == 2) {
        if (count) *count = g_battle_runtime_guide_defaults_count;
        return g_battle_runtime_guide_defaults;
    }
    if (kind == 4) {
        if (count) *count = g_forest_runtime_guide_defaults_count;
        return g_forest_runtime_guide_defaults;
    }
    if (kind == 1) {
        if (count) *count = g_tower_runtime_guide_defaults_count;
        return g_tower_runtime_guide_defaults;
    }
    return NULL;
}

static const char *runtime_stage_display_name(int kind)
{
    if (kind == 2) return "Wasteland/BATTLE";
    if (kind == 1) return "TOWER2/TWGCLOUD";
    if (kind == 4) return "Living Forest";
    if (kind == 3) return "Custom";
    return "Unknown";
}

int mk2_runtime_autoload_stage_recipe(void)
{
    if (g_runtime_recipe_kind == 0) {
        int suggested = mk2_runtime_suggested_preset_for_stage();
        if (suggested)
            runtime_guides_load_preset(suggested);
    }
    return g_runtime_recipe_kind;
}

void tower_runtime_guides_init_once(void)
{
    int kind = mk2_runtime_stage_kind();
    if (g_tower_runtime_guides_init && g_tower_runtime_stage_kind == kind) return;
    int count = 0;
    const RuntimeExtraGuide *defs = runtime_guide_defaults_for_stage(kind, &count);
    if (!defs || count <= 0) {
        g_tower_runtime_guide_n = 0;
        g_tower_runtime_stage_kind = kind;
        g_tower_runtime_guides_init = true;
        return;
    }
    if (count > MAX_RUNTIME_EXTRA_GUIDES) count = MAX_RUNTIME_EXTRA_GUIDES;
    memset(g_tower_runtime_guides, 0, sizeof(g_tower_runtime_guides));
    memcpy(g_tower_runtime_guides, defs, sizeof(RuntimeExtraGuide) * (size_t)count);
    g_tower_runtime_guide_n = count;
    g_tower_runtime_stage_kind = kind;
    g_tower_runtime_guides_init = true;
    g_tower_runtime_guides_dirty = false;
    g_tower_runtime_selected = 0;
}

void runtime_guides_clear_session(void)
{
    memset(g_tower_runtime_guides, 0, sizeof(g_tower_runtime_guides));
    g_tower_runtime_guide_n = 0;
    g_tower_runtime_selected = -1;
    g_tower_runtime_stage_kind = 0;
    g_tower_runtime_guides_init = true;
    g_tower_runtime_guides_dirty = false;
    g_runtime_recipe_kind = 0;
    g_runtime_extras_overlay = false;
    g_runtime_extras_labels = false;
    g_view_changed = 1;
}

static void runtime_guides_load_preset(int kind)
{
    int count = 0;
    const RuntimeExtraGuide *defs = runtime_guide_defaults_for_stage(kind, &count);
    if (!defs || count <= 0) {
        runtime_guides_clear_session();
        return;
    }
    if (count > MAX_RUNTIME_EXTRA_GUIDES) count = MAX_RUNTIME_EXTRA_GUIDES;
    memset(g_tower_runtime_guides, 0, sizeof(g_tower_runtime_guides));
    memcpy(g_tower_runtime_guides, defs, sizeof(RuntimeExtraGuide) * (size_t)count);
    g_tower_runtime_guide_n = count;
    g_tower_runtime_selected = 0;
    g_tower_runtime_stage_kind = kind;
    g_tower_runtime_guides_init = true;
    g_tower_runtime_guides_dirty = false;
    g_runtime_recipe_kind = kind;
    g_runtime_extras_overlay = true;
    g_view_changed = 1;
}

static void tower_runtime_guides_reset(void)
{
    if (g_runtime_recipe_kind == 1 || g_runtime_recipe_kind == 2)
        runtime_guides_load_preset(g_runtime_recipe_kind);
}

static void mk2_runtime_extra_table_row(const char *kind, const char *asset, const char *where, const char *source)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted(kind);
    ImGui::TableNextColumn(); ImGui::TextUnformatted(asset);
    ImGui::TableNextColumn(); ImGui::TextUnformatted(where);
    ImGui::TableNextColumn(); ImGui::TextUnformatted(source);
}

static bool mk2_try_data_file(const char *dir, const char *filename, char *out, size_t outsz)
{
    if (!dir || !dir[0] || !filename || !filename[0]) return false;
    path_join(out, outsz, dir, filename);
    return stage_file_exists(out);
}

static bool mk2_try_root_data_file(const char *root, const char *filename, char *out, size_t outsz)
{
    if (!root || !root[0]) return false;
    char data_dir[512];
    path_join(data_dir, sizeof data_dir, root, "data");
    return mk2_try_data_file(data_dir, filename, out, outsz);
}

static bool mk2_derive_workplace_root(const char *path, char *out, size_t outsz)
{
    if (!path || !path[0] || !out || outsz == 0) return false;
    char clean[512];
    snprintf(clean, sizeof clean, "%s", path);
    for (char *p = clean; *p; p++)
        if (*p == '/') *p = '\\';
    const char *needle = "\\Workplace\\";
    char *hit = strstr(clean, needle);
    if (!hit) return false;
    size_t n = (size_t)(hit - clean) + strlen("\\Workplace");
    if (n >= outsz) n = outsz - 1;
    memcpy(out, clean, n);
    out[n] = '\0';
    return out[0] != '\0';
}

static bool mk2_try_workplace_data_file(const char *workplace,
                                        const char *relative_root,
                                        const char *filename,
                                        char *out,
                                        size_t outsz)
{
    char root[512];
    path_join(root, sizeof root, workplace, relative_root);
    return mk2_try_root_data_file(root, filename, out, outsz);
}

bool mk2_find_sibling_data_file(const char *filename, char *out, size_t outsz)
{
    if (!filename || !out || outsz == 0) return false;
    out[0] = '\0';
    const char *base_path = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    if (base_path && base_path[0]) {
        char stage_dir[512];
        stage_dirname(base_path, stage_dir, sizeof stage_dir);
        if (mk2_try_data_file(stage_dir, filename, out, outsz))
            return true;
    }

    if (mk2_try_root_data_file(g_stage_mk2_root, filename, out, outsz))
        return true;

    char workspace[512];
    if (base_path && mk2_derive_workplace_root(base_path, workspace, sizeof workspace)) {
        if (mk2_try_workplace_data_file(workspace, "mk2-readonly\\mk2-main", filename, out, outsz))
            return true;
        if (mk2_try_workplace_data_file(workspace, "mk2-main", filename, out, outsz))
            return true;
        if (mk2_try_workplace_data_file(workspace, "mk2-rebuild-main", filename, out, outsz))
            return true;
    }

    out[0] = '\0';
    return false;
}

static void mk2_append_runtime_tradeoff_notes(char *out, size_t outsz)
{
    stage_append(out, outsz, "Runtime extras vs all BDB/BDD\n");
    stage_append(out, outsz, "- BDB/BDD is the LOAD2 static background payload: good for fixed art, module packing, palettes, and one-file-pair editing.\n");
    stage_append(out, outsz, "- Runtime extras are BGND.ASM/LOD spawned floors, sprites, animations, clouds, flames, and display-list objects.\n");
    stage_append(out, outsz, "- Runtime extras save video ROM, reuse stock art, animate/skew/scroll naturally, and can sit on exact MK2 display lists.\n");
    stage_append(out, outsz, "- The cost is that the stage is no longer self-contained in BDB/BDD; preview/import must know BGND.ASM plus the LOD/IMG/FRM assets.\n");
    stage_append(out, outsz, "- Flattening everything into BDB/BDD is easier to edit but usually loses animation/skew behavior or costs much more art payload.\n");
}

static void mk2_tower2_runtime_report(char *out, size_t outsz)
{
    out[0] = '\0';
    stage_append(out, outsz, "TOWER2 runtime extras map\n");
    stage_append(out, outsz, "BDB/BDD payload: PLANE4/PLANE5 LOAD2 modules only; these cover static wall/pillar pieces.\n");
    stage_append(out, outsz, "PLANE4BMOD: baklst4 offset 0x016D,-0x01F, pillars/small statue BDB module.\n");
    stage_append(out, outsz, "PLANE5BMOD: baklst6 offset 0x0000,-0x021, wall BDB module.\n");
    stage_append(out, outsz, "Runtime floor: FL_TOW from MK7MIL.LOD, tower_floor_info, y=0x0B5, height 75, skewed by tower_skew_calla.\n");
    stage_append(out, outsz, "Runtime monk: monktorso/monk1..monk7 from MK6MIL assets, spawned by make_big_monk on baklst5.\n");
    stage_append(out, outsz, "Runtime statues: STATUE1/STATUE2 from CASTLE.IMG via CASTLEP.LOD, spawned by spawn_tower_castle_props.\n");
    stage_append(out, outsz, "Runtime flames: FlameA1..FlameD4 from MK6MIL.LOD, animated by tower_flame_proc_l/r on baklst3.\n");
    stage_append(out, outsz, "Runtime clouds: cloud1a..cloud1d from MK6MIL.LOD, animated by cloud_proc on baklst7/baklst8.\n\n");
    mk2_append_runtime_tradeoff_notes(out, outsz);
}

static void mk2_battle_runtime_report(char *out, size_t outsz)
{
    out[0] = '\0';
    stage_append(out, outsz, "Wasteland/BATTLE runtime extras map\n");
    stage_append(out, outsz, "BDB/BDD payload: BAT1/BAT2/BAT4/BAT5/BAT6/BAT7 LOAD2 modules only; these are the static Wasteland wall/sky layers.\n");
    stage_append(out, outsz, "Runtime floor: FL_BATTL from MK7MIL.LOD, battle_floor_info, y=0x0D2, height 45, skewed by floor_code.\n");
    stage_append(out, outsz, "Runtime props: BATTLE.IMG is listed by MK7MIL.LOD and spawned at stage init by spawn_battle_sprites.\n");
    stage_append(out, outsz, "Render list: put_bat_sprite inserts the props into baklst3 before floor_code so the skewed floor masks the lower parts.\n");
    stage_append(out, outsz, "Placed props: RUBLE1, BURN_vda, skulls, SKELTS, ROCK_vda, BURN6_vda chain/hook fire, RUBLE2.\n");
    stage_append(out, outsz, "Import note: BATTLE.IMG contains the source art; BATTLE.BDB alone will not show these runtime-spawned sprites.\n\n");
    mk2_append_runtime_tradeoff_notes(out, outsz);
}

static void draw_tower_runtime_guide_editor(void)
{
    tower_runtime_guides_init_once();
    int count = tower_runtime_guide_count();
    if (g_tower_runtime_selected < 0) g_tower_runtime_selected = 0;
    if (g_tower_runtime_selected >= count) g_tower_runtime_selected = count - 1;

    ImGui::Separator();
    ImGui::Text("Runtime Extra Recipe");
    ImGui::TextDisabled("Load a preset or enter your own runtime sprites. Edit or drag guides, then bake them and use Save BDB + BDD to write files.");
    ImGui::TextDisabled("Drag handles in the stage view. Grid Snap uses the current grid; edges/centers snap to other art even across the map. Hold Alt to bypass snapping.");
    if (g_tower_runtime_guides_dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "modified");
    }

    if (ImGui::Button("Load Tower2 Preset")) {
        runtime_guides_load_preset(1);
        stage_set_toast("Loaded Tower2 runtime extra preset");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Wasteland Preset")) {
        runtime_guides_load_preset(2);
        stage_set_toast("Loaded Wasteland runtime extra preset");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Recipe")) {
        runtime_guides_clear_session();
        stage_set_toast("Runtime recipe cleared");
        count = 0;
    }

    int suggested = mk2_runtime_suggested_preset_for_stage();
    if (suggested && g_runtime_recipe_kind == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                           "This filename looks like %s, but no runtime recipe is loaded.",
                           runtime_stage_display_name(suggested));
        const char *btn_label = suggested == 1 ? "Use Tower2 Runtime Recipe"
                              : suggested == 2 ? "Use Wasteland Runtime Recipe"
                              : suggested == 4 ? "Use Forest Runtime Recipe"
                              : "Use Runtime Recipe";
        if (ImGui::Button(btn_label, ImVec2(-1, 0))) {
            runtime_guides_load_preset(suggested);
            stage_set_toast("Loaded suggested runtime recipe");
        }
    }

    static RuntimeExtraGuide draft = {
        "SPRITE", "Runtime extra", "", 0, 0, 64, 64, 1.0f, 0x40, 0, IM_COL32(110, 210, 125, 95)
    };
    ImGuiTreeNodeFlags add_flags = count == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    if (ImGui::CollapsingHeader("Add Runtime Extra", add_flags)) {
        ImGui::InputText("Asset label", draft.asset, sizeof draft.asset);
        ImGui::InputText("Display name", draft.label, sizeof draft.label);
        ImGui::InputText("LOD / IMG source", draft.source, sizeof draft.source);
        if (ImGui::Button("Browse Source...")) {
            char path[512] = "";
            if (file_dialog_open("Select Runtime Source",
                "Runtime Sources\0*.LOD;*.lod;*.IMG;*.img\0All Files\0*.*\0",
                path, sizeof path))
            {
                snprintf(draft.source, sizeof draft.source, "%s", path);
            }
        }
        ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("X##draft", &draft.x);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("Y##draft", &draft.y);
        ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("W##draft", &draft.w);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("H##draft", &draft.h);
        if (draft.w < 1) draft.w = 1;
        if (draft.h < 1) draft.h = 1;
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Scroll##draft", &draft.scroll, 0.025f, 0.1f, "%.4f");
        int one = 1, sixteen = 16;
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputScalar("Layer wx high byte##draft", ImGuiDataType_S32, &draft.layer,
                           &one, &sixteen, "%02X", ImGuiInputTextFlags_CharsHexadecimal);
        draft.layer &= 0xFF;
        bool draft_hflip = draft.hfl != 0;
        if (ImGui::Checkbox("HFlip##draft", &draft_hflip))
            draft.hfl = draft_hflip ? 1 : 0;
        if (ImGui::Button("Add To Recipe", ImVec2(-1, 0))) {
            if (g_tower_runtime_guide_n < MAX_RUNTIME_EXTRA_GUIDES && draft.asset[0]) {
                if (!draft.label[0]) snprintf(draft.label, sizeof draft.label, "%s", draft.asset);
                g_tower_runtime_guides[g_tower_runtime_guide_n++] = draft;
                g_tower_runtime_selected = g_tower_runtime_guide_n - 1;
                g_tower_runtime_guides_init = true;
                g_tower_runtime_guides_dirty = true;
                g_runtime_recipe_kind = 3;
                g_tower_runtime_stage_kind = 3;
                g_runtime_extras_overlay = true;
                g_view_changed = 1;
                stage_set_toast("Added runtime extra guide");
                count = tower_runtime_guide_count();
            } else {
                stage_set_toast("Runtime recipe is full or missing an asset label");
            }
        }
    }

    if (ImGui::BeginTable("tower_runtime_guide_values", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                          ImVec2(0, 150))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("asset", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("label");
        ImGui::TableSetupColumn("LOD/source", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("x,y", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < count; i++) {
            RuntimeExtraGuide *e = &g_tower_runtime_guides[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idx[16];
            snprintf(idx, sizeof idx, "%d", i);
            if (ImGui::Selectable(idx, g_tower_runtime_selected == i,
                                  ImGuiSelectableFlags_SpanAllColumns))
                g_tower_runtime_selected = i;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e->asset);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e->label);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e->source);
            ImGui::TableNextColumn(); ImGui::Text("%d,%d", e->x, e->y);
            ImGui::TableNextColumn(); ImGui::Text("%dx%d", e->w, e->h);
            ImGui::TableNextColumn(); ImGui::Text("%02X", e->layer & 0xFF);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (g_tower_runtime_selected >= 0 && g_tower_runtime_selected < count) {
        RuntimeExtraGuide *e = &g_tower_runtime_guides[g_tower_runtime_selected];
        bool changed = false;
        ImGui::PushID("runtime_guide_edit");
        changed |= ImGui::InputText("Asset", e->asset, sizeof e->asset);
        changed |= ImGui::InputText("Label", e->label, sizeof e->label);
        changed |= ImGui::InputText("LOD / Source", e->source, sizeof e->source);
        if (ImGui::Button("Browse Selected Source...")) {
            char path[512] = "";
            if (file_dialog_open("Select Runtime Source",
                "Runtime Sources\0*.LOD;*.lod;*.IMG;*.img\0All Files\0*.*\0",
                path, sizeof path))
            {
                snprintf(e->source, sizeof e->source, "%s", path);
                changed = true;
            }
        }
        changed |= ImGui::InputInt("X", &e->x);
        changed |= ImGui::InputInt("Y", &e->y);
        changed |= ImGui::InputInt("W", &e->w);
        changed |= ImGui::InputInt("H", &e->h);
        if (e->w < 1) e->w = 1;
        if (e->h < 1) e->h = 1;
        changed |= ImGui::InputFloat("Scroll", &e->scroll, 0.025f, 0.1f, "%.4f");
        int one = 1, sixteen = 16;
        changed |= ImGui::InputScalar("Layer wx high byte", ImGuiDataType_S32, &e->layer,
                                      &one, &sixteen, "%02X",
                                      ImGuiInputTextFlags_CharsHexadecimal);
        e->layer &= 0xFF;
        bool hflip = e->hfl != 0;
        if (ImGui::Checkbox("HFlip", &hflip)) {
            e->hfl = hflip ? 1 : 0;
            changed = true;
        }

        int step = ImGui::GetIO().KeyShift ? 8 : 1;
        ImGui::TextDisabled("Nudge selected guide (%d px, hold Shift for 8 px)", step);
        if (ImGui::SmallButton("<")) { e->x -= step; changed = true; } ImGui::SameLine();
        if (ImGui::SmallButton(">")) { e->x += step; changed = true; } ImGui::SameLine();
        if (ImGui::SmallButton("^")) { e->y -= step; changed = true; } ImGui::SameLine();
        if (ImGui::SmallButton("v")) { e->y += step; changed = true; }

        if (changed) {
            g_tower_runtime_guides_dirty = true;
            g_runtime_extras_overlay = true;
            g_view_changed = 1;
        }
        ImGui::PopID();
    }

    if (g_runtime_recipe_kind == 1 || g_runtime_recipe_kind == 2) {
        if (ImGui::Button("Reset Preset Guide Values", ImVec2(-1, 0))) {
            tower_runtime_guides_reset();
            g_view_changed = 1;
        }
    }
}

void draw_mk2_runtime_extras_tool(void)
{
    ImGui::Text("Runtime Extras");
    ImGui::TextDisabled("Some stages mix LOAD2 BDB/BDD modules with runtime-spawned floors, sprites, and animations.");
    ImGui::TextDisabled("Current stage: %s  objects:%d images:%d", g_name[0] ? g_name : "(none)", g_no, g_ni);
    ImGui::TextDisabled("Recipes are manual: no stock filename guessing or hidden auto-load.");
    ImGui::Checkbox("Show Runtime Extras Overlay", &g_runtime_extras_overlay);
    ImGui::SameLine();
    ImGui::Checkbox("Labels", &g_runtime_extras_labels);
    bool have_recipe = tower_runtime_guide_count() > 0;
    if (!have_recipe) ImGui::BeginDisabled();
    if (ImGui::Button("Delete All Baked Runtime Placements", ImVec2(-1, 0))) {
        int removed = delete_all_runtime_guide_objects(true);
        char msg[160];
        snprintf(msg, sizeof msg, removed ? "Deleted %d runtime placement(s)" : "No baked runtime placements found", removed);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Strip Runtime Extras From BDB/BDD", ImVec2(-1, 0))) {
        int removed_objects = 0, removed_images = 0;
        int total = delete_runtime_guide_images_and_objects(&removed_objects, &removed_images);
        g_runtime_extras_overlay = false;
        char msg[192];
        snprintf(msg, sizeof msg,
                 total ? "Stripped %d placement(s), %d imported image(s)"
                       : "No runtime extras found",
                 removed_objects, removed_images);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Hide Runtime Guides This Session", ImVec2(-1, 0))) {
        int hidden = hide_all_runtime_guides_for_session();
        char msg[128];
        snprintf(msg, sizeof msg, hidden ? "Hidden %d runtime guide(s)" : "No runtime guides to hide", hidden);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Bake Edited Extras To BDB Objects", ImVec2(-1, 0))) {
        int added = mk2_bake_runtime_guides_to_bdb(true, true);
        char msg[128];
        snprintf(msg, sizeof msg, added ? "Baked %d runtime extra object(s)" : "No runtime extras baked", added);
        stage_set_toast(msg);
    }
    if (ImGui::Button("Import Runtime LOD Sprite Sources", ImVec2(-1, 0))) {
        int n = import_runtime_lod_sources_for_active_guides(true);
        char msg[160];
        snprintf(msg, sizeof msg, n > 0 ? "Imported %d runtime LOD sprite(s)" : "No missing runtime LOD sprites imported", n);
        stage_set_toast(msg);
    }
    if (!have_recipe) ImGui::EndDisabled();

    if (ImGui::Button("Copy Selected As Runtime Recipe", ImVec2(-1, 0))) {
        mk2_copy_selected_runtime_recipe();
    }

    if (ImGui::Button("Import Runtime IMG Source...", ImVec2(-1, 0))) {
        char img_path[512] = "";
        if (file_dialog_open("Import Runtime IMG Source",
            "Midway IMG Files\0*.IMG;*.img\0All Files\0*.*\0",
            img_path, sizeof img_path))
        {
            int start = g_ni;
            int pal_start = g_n_pals;
            int n = import_img_file(img_path);
            if (n > 0) {
                lod_tag_imported_range(start, g_ni, path_basename_ptr(img_path));
                runtime_actor_mark_preview_import_range(start, pal_start,
                                                        start, g_ni,
                                                        img_path);
                g_show_images = true;
            }
            char msg[160];
            snprintf(msg, sizeof msg, n > 0 ? "Imported %d runtime IMG sprite(s)" : "No IMG sprites imported", n);
            stage_set_toast(msg);
        }
    }

    if (!g_have_bdb) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Open a BDB/BDD before baking runtime extras into objects.");
    } else if (!mk2_has_drawable_stage()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "This project has no loaded BDB/BDD objects or images yet.");
    }

    int runtime_kind = mk2_runtime_stage_kind();
    if (!runtime_kind) {
        ImGui::TextWrapped("No runtime recipe is loaded. Use a preset, or add entries with asset label, source file, position, size, layer, and scroll.");
        ImGui::TextDisabled("Pattern: BDB/BDD holds static modules; runtime LOD/IMG sources add floors, props, animations, and skewed pieces.");
    } else if (runtime_kind == 2) {
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "Wasteland preset loaded.");
        ImGui::TextDisabled("Review every source path before import. You can browse per-guide sources below.");
    } else if (runtime_kind == 1) {
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "Tower2 preset loaded.");
        ImGui::TextDisabled("Review every source path before import. You can browse per-guide sources below.");
    } else if (runtime_kind == 4) {
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "Living Forest preset loaded.");
        ImGui::TextDisabled("Review every source path before import. You can browse per-guide sources below.");
    } else {
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "Custom runtime recipe loaded.");
    }
    draw_tower_runtime_guide_editor();

    char button_label[96];
    snprintf(button_label, sizeof button_label, "Copy %s Runtime Notes", runtime_stage_display_name(runtime_kind));
    if (ImGui::Button(button_label, ImVec2(-1, 0))) {
        char report[4096];
        if (runtime_kind == 2) mk2_battle_runtime_report(report, sizeof report);
        else if (runtime_kind == 1) mk2_tower2_runtime_report(report, sizeof report);
        else {
            report[0] = '\0';
            mk2_append_runtime_tradeoff_notes(report, sizeof report);
        }
        ImGui::SetClipboardText(report);
        stage_set_toast("Copied runtime notes");
    }
}

