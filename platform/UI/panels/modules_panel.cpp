#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/world_module_utils.h"
#include "imgui.h"
#include "undo_manager.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static int  g_edit_mod_idx = -1;
static char g_mod_edit_buf[256];
static int  g_module_line_capture_active = -1;
static int  g_module_line_capture_capacity = 0;
static std::vector<char> g_module_line_before;
static std::vector<unsigned char> g_module_line_mask;

static void module_line_capture_reset(void)
{
    g_module_line_capture_active = -1;
    g_module_line_capture_capacity = 0;
    g_module_line_before.clear();
    g_module_line_mask.clear();
}

static bool module_line_capture_mask(const unsigned char *mask)
{
    int module_cap = editor_project_module_capacity();
    if (!mask || module_cap <= 0)
        return false;

    g_module_line_capture_capacity = module_cap;
    g_module_line_before.assign((size_t)module_cap * 256u, 0);
    g_module_line_mask.assign((size_t)module_cap, 0);

    bool any = false;
    for (int i = 0; i < g_bdb_num_modules && i < module_cap; i++) {
        memcpy(g_module_line_before.data() + ((size_t)i * 256u),
               g_bdb_modules[i], 256);
        if (mask[i]) {
            g_module_line_mask[(size_t)i] = 1;
            any = true;
        }
    }
    if (!any) {
        module_line_capture_reset();
        return false;
    }
    g_module_line_capture_active = -2;
    return true;
}

static bool module_line_capture_one(int module_idx)
{
    int module_cap = editor_project_module_capacity();
    if (module_idx < 0 || module_idx >= g_bdb_num_modules || module_cap <= 0)
        return false;
    if (g_module_line_capture_active == module_idx)
        return true;
    if (g_module_line_capture_active >= 0 || g_module_line_capture_active == -2)
        undo_save_module_line_delta_for_mask(g_module_line_before.data(),
                                             g_module_line_mask.data(),
                                             g_module_line_capture_capacity,
                                             "Edit Module");
    std::vector<unsigned char> mask((size_t)module_cap, 0);
    mask[(size_t)module_idx] = 1;
    if (!module_line_capture_mask(mask.data()))
        return false;
    g_module_line_capture_active = module_idx;
    return true;
}

static int module_line_commit(const char *label)
{
    int changed = 0;
    if (g_module_line_capture_active >= 0 || g_module_line_capture_active == -2) {
        changed = undo_save_module_line_delta_for_mask(g_module_line_before.data(),
                                                       g_module_line_mask.data(),
                                                       g_module_line_capture_capacity,
                                                       label ? label : "Edit Module");
    }
    module_line_capture_reset();
    return changed;
}

static void module_rewrite_bounds(int module_idx, const char *name,
                                  int x1, int x2, int y1, int y2)
{
    if (module_idx < 0 || module_idx >= g_bdb_num_modules) return;
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d",
             (name && name[0]) ? name : "MOD", x1, x2, y1, y2);
    if (!editor_project_set_module_line(module_idx, line)) return;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_view_changed = 1;
}

static bool module_bounds_from_objects(bool selected_only,
                                       int *out_x1, int *out_x2,
                                       int *out_y1, int *out_y2,
                                       int *out_count)
{
    int x1 = INT_MAX, x2 = INT_MIN, y1 = INT_MAX, y2 = INT_MIN;
    int count = 0;
    for (int i = 0; i < g_no; i++) {
        if (selected_only && !g_sel_flags[i]) continue;
        Img *im = img_find(g_obj[i].ii);
        int ow = im ? im->w : 1;
        int oh = im ? im->h : 1;
        int ox1 = g_obj[i].depth;
        int oy1 = g_obj[i].sy;
        int ox2 = ox1 + ow - 1;
        int oy2 = oy1 + oh - 1;
        if (ox1 < x1) x1 = ox1;
        if (ox2 > x2) x2 = ox2;
        if (oy1 < y1) y1 = oy1;
        if (oy2 > y2) y2 = oy2;
        count++;
    }
    if (out_count) *out_count = count;
    if (count <= 0) return false;
    if (out_x1) *out_x1 = x1;
    if (out_x2) *out_x2 = x2;
    if (out_y1) *out_y1 = y1;
    if (out_y2) *out_y2 = y2;
    return true;
}

static bool module_bounds_for_stage(int *out_x1, int *out_x2, int *out_y1, int *out_y2)
{
    int x1 = 0, y1 = 0, x2 = -1, y2 = -1;
    int ww = 0, wh = 0;
    bool have = false;
    if (get_world_size(&ww, &wh) && ww > 0 && wh > 0) {
        x1 = 0;
        y1 = 0;
        x2 = ww - 1;
        y2 = wh - 1;
        have = true;
    }

    int ox1 = 0, ox2 = 0, oy1 = 0, oy2 = 0, count = 0;
    if (module_bounds_from_objects(false, &ox1, &ox2, &oy1, &oy2, &count)) {
        if (!have || ox1 < x1) x1 = ox1;
        if (!have || ox2 > x2) x2 = ox2;
        if (!have || oy1 < y1) y1 = oy1;
        if (!have || oy2 > y2) y2 = oy2;
        have = true;
    }

    if (!have) return false;
    if (out_x1) *out_x1 = x1;
    if (out_x2) *out_x2 = x2;
    if (out_y1) *out_y1 = y1;
    if (out_y2) *out_y2 = y2;
    return true;
}

static int module_create_with_bounds(const char *name, int x1, int x2, int y1, int y2)
{
    if (!editor_project_reserve_modules(g_bdb_num_modules + 1)) {
        stage_set_toast("Module limit reached");
        return -1;
    }
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    char mod_name[64];
    snprintf(mod_name, sizeof mod_name, "%s",
             (name && name[0]) ? name : "MOD");
    /* "MOD" is the auto-name sentinel; an explicit name that collides with an
       existing module gets disambiguated the same way, so a duplicate module
       name (which breaks both module-drag and BGND.ASM BMOD matching) can't
       happen here either. */
    if (strcmp(mod_name, "MOD") == 0 || module_name_in_use(mod_name, -1)) {
        char base[64];
        snprintf(base, sizeof base, "%s", mod_name);
        module_generate_unique_name(mod_name, sizeof mod_name, base);
    }

    undo_save_ex("Create Module");
    char line[256];
    snprintf(line, sizeof line, "%s %d %d %d %d", mod_name, x1, x2, y1, y2);
    int created = editor_project_insert_module_line_before_enclosing(line, x1, x2, y1, y2);
    if (created < 0)
        return -1;
    sync_bdb_header_counts();
    g_dirty = 1;
    g_view_changed = 1;
    g_show_module_bounds = true;
    char msg[128];
    snprintf(msg, sizeof msg, "Created module %s (%dx%d)",
             mod_name, x2 - x1 + 1, y2 - y1 + 1);
    stage_set_toast(msg);
    return created;
}

static void draw_module_bounds_size_editor(int module_idx, bool show_bounds)
{
    char name[64] = "";
    int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    if (!parse_module_bounds(module_idx, name, &x1, &x2, &y1, &y2)) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Could not parse module bounds.");
        return;
    }

    bool bounds_changed = false;
    bool commit_after_edit = false;
    const char *commit_label = "Edit Module";
    if (show_bounds) {
        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::InputInt("X start", &x1)) bounds_changed = true;
        if (ImGui::IsItemActivated()) module_line_capture_one(module_idx);
        if (ImGui::IsItemDeactivatedAfterEdit()) { commit_after_edit = true; commit_label = "Edit Module Bounds"; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::InputInt("X end", &x2)) bounds_changed = true;
        if (ImGui::IsItemActivated()) module_line_capture_one(module_idx);
        if (ImGui::IsItemDeactivatedAfterEdit()) { commit_after_edit = true; commit_label = "Edit Module Bounds"; }

        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::InputInt("Y start", &y1)) bounds_changed = true;
        if (ImGui::IsItemActivated()) module_line_capture_one(module_idx);
        if (ImGui::IsItemDeactivatedAfterEdit()) { commit_after_edit = true; commit_label = "Edit Module Bounds"; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::InputInt("Y end", &y2)) bounds_changed = true;
        if (ImGui::IsItemActivated()) module_line_capture_one(module_idx);
        if (ImGui::IsItemDeactivatedAfterEdit()) { commit_after_edit = true; commit_label = "Edit Module Bounds"; }
    }

    int width = x2 - x1 + 1;
    int height = y2 - y1 + 1;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    bool size_changed = false;
    ImGui::SetNextItemWidth(92.0f);
    if (ImGui::InputInt("Width", &width)) size_changed = true;
    if (ImGui::IsItemActivated()) module_line_capture_one(module_idx);
    if (ImGui::IsItemDeactivatedAfterEdit()) { commit_after_edit = true; commit_label = "Resize Module"; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sets X end from X start using inclusive module bounds.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(92.0f);
    if (ImGui::InputInt("Height", &height)) size_changed = true;
    if (ImGui::IsItemActivated()) module_line_capture_one(module_idx);
    if (ImGui::IsItemDeactivatedAfterEdit()) { commit_after_edit = true; commit_label = "Resize Module"; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sets Y end from Y start using inclusive module bounds.");

    if (size_changed) {
        if (width < 1) width = 1;
        if (height < 1) height = 1;
        x2 = x1 + width - 1;
        y2 = y1 + height - 1;
    }

    if ((bounds_changed || size_changed) && g_module_line_capture_active < 0)
        module_line_capture_one(module_idx);
    if (bounds_changed || size_changed)
        module_rewrite_bounds(module_idx, name, x1, x2, y1, y2);
    if (commit_after_edit)
        module_line_commit(commit_label);
}

static bool runtime_name_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return false;
    }
    return *a == *b;
}

static const char *runtime_parallax_label(float s)
{
    if (s <= 0.01f) return "screen-fixed";
    if (s > 0.98f && s < 1.02f) return "playfield";
    return s < 1.0f ? "far (slow)" : "near (fast)";
}

/* Per-module runtime binding from BGND.ASM: where the stage opens, how far it
   scrolls, and the parallax plane each module rides. World position itself comes
   from the module bounds above; this is the assembly-side placement that bddtool
   normally cannot touch. Camera, scroll limits, per-module parallax and runtime
   placement are all editable here and write BGND.ASM with a timestamped backup;
   the overview table stays read-only. */
static void draw_module_runtime_binding(void)
{
    if (g_runtime_binding_jump_module >= 0)
        ImGui::SetNextItemOpen(true);
    if (!ImGui::CollapsingHeader("Runtime Binding (BGND.ASM)"))
        return;

    ImGui::TextWrapped(
        "Module bounds above set world position. The values here come from the "
        "stage's BGND.ASM and decide where the stage opens, how far it scrolls, and "
        "which parallax plane each module rides. Applying edits rewrites BGND.ASM "
        "after saving a timestamped .pre_* backup.");

    /* Reload the editable fields whenever the loaded stage changes. */
    static char loaded_stage[160] = "";
    static int cam_x = 0, cam_y = 0, cam_ok = 0;
    static int lim_l = 0, lim_r = 0, lim_ok = 0;
    static int rb_sel = -1, rb_loaded = -2;
    if (g_runtime_binding_jump_module >= 0 && g_runtime_binding_jump_module < g_bdb_num_modules) {
        rb_sel = g_runtime_binding_jump_module;
        rb_loaded = -2;
        g_runtime_binding_jump_module = -1;
    }
    static float rb_factor = 1.0f;
    static int rb_ox = 0, rb_oy = 0;
    static bool rb_placed = false;
    static float bg_rgb[3] = { 0, 0, 0 };
    static int bg_ok = 0;
    char key[160];
    snprintf(key, sizeof key, "%s|%s", g_name, g_bdb_path);
    if (strncmp(key, loaded_stage, sizeof loaded_stage) != 0) {
        snprintf(loaded_stage, sizeof loaded_stage, "%s", key);
        cam_ok = bdd_get_stage_start_camera(&cam_x, &cam_y);
        lim_ok = bdd_get_stage_scroll_limits(&lim_l, &lim_r);
        int rgb555 = 0;
        bg_ok = bdd_get_stage_bg_color(&rgb555);
        bg_rgb[0] = ((rgb555 >> 10) & 31) / 31.0f;
        bg_rgb[1] = ((rgb555 >> 5) & 31) / 31.0f;
        bg_rgb[2] = (rgb555 & 31) / 31.0f;
        rb_sel = -1;
        rb_loaded = -2;
    }

    {
        char draft_path[640];
        if (stage_draft_bgnd_path(draft_path, sizeof draft_path)) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                "Editing a DRAFT (%s) -- the shared BGND.ASM is untouched.", draft_path);
            if (ImGui::Button("Promote to BGND.ASM", ImVec2(-1, 0)))
                stage_promote_draft_to_bgnd();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Backs up the real BGND.ASM with a datestamp, then merges this "
                                  "draft's block and a table_o_mods entry into a new one.\n"
                                  "MKSEL.ASM stage-select wiring is still a separate manual step.");
        } else {
            ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.65f, 1.0f),
                "Editing the real, shared BGND.ASM directly.");
        }
    }

    ImGui::SeparatorText("Stage open + scroll");
    if (ImGui::SmallButton("Reload from BGND.ASM")) {
        cam_ok = bdd_get_stage_start_camera(&cam_x, &cam_y);
        lim_ok = bdd_get_stage_scroll_limits(&lim_l, &lim_r);
    }

    ImGui::SetNextItemWidth(96.0f); ImGui::InputInt("Open camera X", &cam_x);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f); ImGui::InputInt("Y", &cam_y);
    if (!cam_ok) ImGui::TextDisabled("No start camera parsed yet — values shown are defaults.");
    if (ImGui::Button("Apply Open Camera to BGND.ASM", ImVec2(-1, 0))) {
        g_stage_start_camera_x = cam_x;
        g_stage_start_camera_y = cam_y;
        g_stage_start_camera_enabled = true;
        stage_start_apply_bgnd_patch();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sets where the camera opens (worldx/worldy) in <stage>_mod.");

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::SetNextItemWidth(96.0f); ImGui::InputInt("Scroll left", &lim_l);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f); ImGui::InputInt("right", &lim_r);
    if (!lim_ok) ImGui::TextDisabled("No scroll limits parsed.");
    if (ImGui::Button("Apply Scroll Limits to BGND.ASM", ImVec2(-1, 0)))
        stage_start_apply_bgnd_limits(lim_l, lim_r);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("worldx range the camera may scroll between (<stage>_mod words 5,6).");

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::SetNextItemWidth(200.0f);
    ImGui::ColorEdit3("Background color", bg_rgb, ImGuiColorEditFlags_NoInputs);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The level's in-game backdrop (autoerase/irqskye colour, <stage>_mod\n"
                          "word 1). MK2 uses RGB555, so each channel snaps to 32 levels.\n"
                          "This is NOT the editor canvas colour under View > Background Color.");
    if (!bg_ok) ImGui::TextDisabled("No background colour parsed (stage may inherit the previous one).");
    if (ImGui::Button("Apply Background Color to BGND.ASM", ImVec2(-1, 0))) {
        int r5 = (int)(bg_rgb[0] * 31.0f + 0.5f);
        int g5 = (int)(bg_rgb[1] * 31.0f + 0.5f);
        int b5 = (int)(bg_rgb[2] * 31.0f + 0.5f);
        if (stage_bgnd_set_bg_color(r5, g5, b5)) bg_ok = 1;
    }

    if (g_stage_start_status[0])
        ImGui::TextWrapped("%s", g_stage_start_status);

    ImGui::SeparatorText("Module planes (parallax)");
    int plane_count = bdd_stage_plane_count();
    if (plane_count <= 0) {
        ImGui::TextDisabled("No <stage>_mod plane table found in BGND.ASM for this stage.");
        if (ImGui::Button("Create Draft BGND.ASM", ImVec2(-1, 0)))
            stage_create_draft_bgnd();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Writes %s.BGND.ASM next to this stage's BDB/BDD with a starter "
                              "_mod/scroll/dlists/calla block using its current modules. Doesn't "
                              "touch the real BGND.ASM until you Promote it.", g_name);
        return;
    }
    ImGui::TextWrapped(
        "Parallax is read from BGND.ASM. 1.00x scrolls with the playfield; under 1 "
        "sits further back (slower); 0.00x is locked to the screen. To re-bind a "
        "module to a different plane, move its *BMOD in <stage>_mod.");

    {
        int not_placed = 0;
        for (int m = 0; m < g_bdb_num_modules; m++) {
            char mn[64] = "";
            if (sscanf(g_bdb_modules[m], "%63s", mn) != 1) continue;
            bool found_m = false;
            for (int p = 0; p < plane_count && !found_m; p++) {
                char pn[32];
                if (bdd_stage_plane_info(p, pn, sizeof pn, NULL, NULL, NULL, NULL) &&
                    runtime_name_ieq(pn, mn))
                    found_m = true;
            }
            if (!found_m) not_placed++;
        }
        if (not_placed > 1) {
            char lbl[64];
            snprintf(lbl, sizeof lbl, "Re-bind all %d not-placed modules", not_placed);
            if (ImGui::Button(lbl, ImVec2(-1, 0))) {
                int bound = 0;
                for (int m = 0; m < g_bdb_num_modules; m++) {
                    char mn[64] = ""; int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
                    if (sscanf(g_bdb_modules[m], "%63s %d %d %d %d", mn, &mx1, &mx2, &my1, &my2) < 1) continue;
                    bool found_m = false;
                    for (int p = 0; p < plane_count && !found_m; p++) {
                        char pn[32];
                        if (bdd_stage_plane_info(p, pn, sizeof pn, NULL, NULL, NULL, NULL) &&
                            runtime_name_ieq(pn, mn))
                            found_m = true;
                    }
                    if (!found_m && stage_bgnd_create_module_placement(mn, mx1, my1))
                        bound++;
                }
                char msg[64]; snprintf(msg, sizeof msg, "Re-bound %d module(s)", bound);
                stage_set_toast(msg);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Adds a *BMOD entry for every not-placed module below, each at its "
                                  "own current position so none of them move. Useful after renaming "
                                  "modules whose old names are still in BGND.ASM/the draft.");
        }
    }

    if (ImGui::BeginTable("module_planes", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("module");
        ImGui::TableSetupColumn("parallax", ImGuiTableColumnFlags_WidthFixed, 132.0f);
        ImGui::TableSetupColumn("offset", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn("draw", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableHeadersRow();
        for (int m = 0; m < g_bdb_num_modules; m++) {
            char mn[64] = ""; int mod_x1 = 0, mod_x2 = 0, mod_y1 = 0, mod_y2 = 0;
            if (sscanf(g_bdb_modules[m], "%63s %d %d %d %d", mn, &mod_x1, &mod_x2, &mod_y1, &mod_y2) < 1) continue;

            int ox = 0, oy = 0, rank = -1, found = 0;
            float scroll = 1.0f;
            char pn[32];
            for (int p = 0; p < plane_count; p++) {
                int pox = 0, poy = 0, prank = -1;
                float pscroll = 1.0f;
                if (!bdd_stage_plane_info(p, pn, sizeof pn, &pox, &poy, &pscroll, &prank))
                    continue;
                if (runtime_name_ieq(pn, mn)) {
                    ox = pox; oy = poy; rank = prank; scroll = pscroll; found = 1;
                    break;
                }
            }

            ImGui::TableNextRow();
            ImGui::PushID(m);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(mn);
            if (ImGui::BeginPopupContextItem("##mod_runtime_ctx")) {
                if (!found) {
                    if (ImGui::MenuItem("Set as runtime location")) {
                        /* Default offset = the module's own world position, so binding it
                           doesn't suddenly shift it from wherever it's anchored today. */
                        if (stage_bgnd_create_module_placement(mn, mod_x1, mod_y1)) {
                            rb_sel = m;
                            rb_loaded = -2;
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Adds a new *BMOD entry to BGND.ASM on the next free\nbackground plane, so this module is drawn at runtime.\nSet its parallax afterward in \"Edit placement & parallax\" below.");
                } else {
                    if (ImGui::MenuItem("Edit runtime placement"))
                        { rb_sel = m; rb_loaded = -2; }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Jumps to this module in \"Edit placement & parallax\" below.");
                }
                ImGui::EndPopup();
            }
            if (!found) {
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "not placed");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("No %sBMOD entry in <stage>_mod, so this module is not drawn at runtime.\nRight-click the module name to set it as a runtime location.", mn);
                ImGui::TableNextColumn(); ImGui::TextDisabled("-");
                ImGui::TableNextColumn(); ImGui::TextDisabled("-");
                ImGui::PopID();
                continue;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.2fx %s", scroll, runtime_parallax_label(scroll));
            ImGui::TableNextColumn();
            ImGui::Text("%d,%d", ox, oy);
            ImGui::TableNextColumn();
            if (rank >= 0) ImGui::Text("%d", rank);
            else ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    /* Editable placement + parallax for one module (writes BGND.ASM). */
    ImGui::SeparatorText("Edit placement & parallax");
    if (g_bdb_num_modules <= 0) {
        ImGui::TextDisabled("No modules to edit.");
        return;
    }
    if (rb_sel < 0 || rb_sel >= g_bdb_num_modules) rb_sel = 0;

    char cur_name[64] = "";
    sscanf(g_bdb_modules[rb_sel], "%63s", cur_name);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Module##rb", cur_name)) {
        for (int m = 0; m < g_bdb_num_modules; m++) {
            char nm[64] = "";
            sscanf(g_bdb_modules[m], "%63s", nm);
            bool sel = (m == rb_sel);
            if (ImGui::Selectable(nm, sel)) rb_sel = m;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    /* Seed the edit fields from the parsed plane info when the pick changes. */
    if (rb_loaded != rb_sel) {
        rb_loaded = rb_sel;
        /* Default offset = the module's own world position: binding with this default
           doesn't shift it from wherever it's anchored today in Runtime Layout view. */
        int seed_x1 = 0, seed_y1 = 0;
        parse_module_bounds(rb_sel, NULL, &seed_x1, NULL, &seed_y1, NULL);
        rb_factor = 1.0f; rb_ox = seed_x1; rb_oy = seed_y1; rb_placed = false;
        char want[64] = "";
        sscanf(g_bdb_modules[rb_sel], "%63s", want);
        int pc = bdd_stage_plane_count();
        for (int p = 0; p < pc; p++) {
            char pn[32]; int pox = 0, poy = 0, prank = -1; float ps = 1.0f;
            if (bdd_stage_plane_info(p, pn, sizeof pn, &pox, &poy, &ps, &prank) &&
                runtime_name_ieq(pn, want)) {
                rb_factor = ps; rb_ox = pox; rb_oy = poy; rb_placed = true;
                break;
            }
        }
    }

    ImGui::TextDisabled("Parallax: 1.00 = moves with playfield, lower = further back, 0.00 = locked to screen.");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderFloat("Parallax##rb", &rb_factor, 0.0f, 1.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button("Apply parallax")) {
        char nm[64] = ""; sscanf(g_bdb_modules[rb_sel], "%63s", nm);
        stage_bgnd_set_module_parallax(nm, rb_factor);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Writes the plane's scroll rate in BGND.ASM.\nAffects every module sharing the same baklst plane.");

    if (!rb_placed)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "Not placed yet -- Apply placement will create a new runtime location.");
    ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("Place X##rb", &rb_ox);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("Y##rb", &rb_oy);
    ImGui::SameLine();
    if (ImGui::Button("Apply placement")) {
        char nm[64] = ""; sscanf(g_bdb_modules[rb_sel], "%63s", nm);
        if (rb_placed) {
            stage_bgnd_set_module_offset(nm, rb_ox, rb_oy);
        } else if (stage_bgnd_create_module_placement(nm, rb_ox, rb_oy)) {
            rb_placed = true;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(rb_placed
            ? "Writes the module's runtime screen offset (.word x,y after its *BMOD)."
            : "Adds a new *BMOD entry to BGND.ASM on the next free background plane.");

    if (g_stage_start_status[0])
        ImGui::TextWrapped("%s", g_stage_start_status);
}

void draw_modules(void)
{
    right_panel_set_next(RIGHT_PANEL_MODULES);
    bool open = ImGui::Begin(g_simple_mode ? "Regions" : "Modules", NULL);
    right_panel_after_begin(RIGHT_PANEL_MODULES);
    if (!open) {
        ImGui::End();
        return;
    }

    if (!g_have_bdb) {
        ImGui::TextUnformatted(g_simple_mode ? "No stage loaded." : "No BDB loaded.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Modules/regions are export boxes. Floor art uses Layer: Floor/play 0x40.");
    ImGui::Checkbox("Show module bounds in viewport", &g_show_module_bounds);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draws source BDB module rectangles. Turn off Runtime Layout to line them up with source coordinates.");

    ImGui::SeparatorText(g_simple_mode ? "Create Region" : "Create Module");
    int selected_n = selected_count();
    int module_cap = editor_project_module_capacity();
    bool at_module_cap = g_bdb_num_modules >= module_cap;
    bool have_selection_bounds = false;
    bool have_stage_bounds = false;
    int sx1 = 0, sx2 = 0, sy1 = 0, sy2 = 0, sel_count_for_bounds = 0;
    int wx1 = 0, wx2 = 0, wy1 = 0, wy2 = 0;
    have_selection_bounds = module_bounds_from_objects(true, &sx1, &sx2, &sy1, &sy2, &sel_count_for_bounds);
    have_stage_bounds = module_bounds_for_stage(&wx1, &wx2, &wy1, &wy2);

    if (at_module_cap) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                           "Module limit reached: %d / %d", g_bdb_num_modules, module_cap);
    } else if (g_bdb_num_modules == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                           "No modules yet. Create one before relying on LOAD2 export.");
    }

    if (at_module_cap || !have_selection_bounds) ImGui::BeginDisabled();
    if (ImGui::Button(g_simple_mode ? "+ From Selection" : "+ Module From Selection", ImVec2(-1, 0))) {
        char name[64];
        module_generate_unique_name(name, sizeof name, "SEL");
        module_create_with_bounds(name, sx1, sx2, sy1, sy2);
    }
    if (at_module_cap || !have_selection_bounds) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Creates bounds around the full selected sprite rectangles. Selected objects: %d", selected_n);

    if (at_module_cap || !have_stage_bounds) ImGui::BeginDisabled();
    if (ImGui::Button(g_simple_mode ? "+ Cover Stage" : "+ Module Covering Stage", ImVec2(-1, 0))) {
        char name[64] = "";
        if (g_bdb_num_modules == 0) module_generate_unique_name(name, sizeof name, "TSTMOD");
        module_create_with_bounds(name[0] ? name : NULL, wx1, wx2, wy1, wy2);
    }
    if (at_module_cap || !have_stage_bounds) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Creates a non-empty module from the BDB world size plus any placed object bounds.");

    if (!have_selection_bounds && !have_stage_bounds)
        ImGui::TextDisabled("Place an object or load a stage before creating module bounds.");

    if (g_simple_mode && g_no > 0) {
        if (ImGui::Button("Fit Existing to Objects", ImVec2(-1, 0))) {
            int wx_min = INT_MAX, wx_max = INT_MIN, wy_min = INT_MAX, wy_max = INT_MIN;
            for (int i = 0; i < g_no; i++) {
                Img *im = img_find(g_obj[i].ii);
                int ow = im ? im->w : 1, oh = im ? im->h : 1;
                if (g_obj[i].depth          < wx_min) wx_min = g_obj[i].depth;
                if (g_obj[i].depth + ow - 1 > wx_max) wx_max = g_obj[i].depth + ow - 1;
                if (g_obj[i].sy             < wy_min) wy_min = g_obj[i].sy;
                if (g_obj[i].sy + oh - 1    > wy_max) wy_max = g_obj[i].sy + oh - 1;
            }
            if (wx_min < wx_max && wy_min < wy_max) {
                if (g_bdb_num_modules == 0) {
                    undo_save();
                    char line[256];
                    snprintf(line, sizeof line, "MOD0 %d %d %d %d", wx_min, wx_max, wy_min, wy_max);
                    editor_project_set_single_module_line(line);
                } else {
                    std::vector<unsigned char> mask((size_t)module_cap, 0);
                    for (int m = 0; m < g_bdb_num_modules && m < module_cap; m++)
                        mask[(size_t)m] = 1;
                    module_line_capture_mask(mask.data());
                    for (int m = 0; m < g_bdb_num_modules; m++) {
                        char mn[64] = ""; int mx0=0,mx1=0,my0=0,my1=0;
                        sscanf(g_bdb_modules[m], "%63s %d %d %d %d", mn, &mx0, &mx1, &my0, &my1);
                        if (wx_min < mx0) mx0 = wx_min;
                        if (wx_max > mx1) mx1 = wx_max;
                        if (wy_min < my0) my0 = wy_min;
                        if (wy_max > my1) my1 = wy_max;
                        char line[256];
                        snprintf(line, sizeof line, "%s %d %d %d %d", mn, mx0, mx1, my0, my1);
                        editor_project_set_module_line(m, line);
                    }
                    module_line_commit("Fit Module Bounds");
                }
                sync_bdb_header_counts();
                g_dirty = 1;
                stage_set_toast("Fit module bounds to all objects");
            }
        }
    }

    if (!g_simple_mode) ImGui::SameLine();
    if (!g_simple_mode && ImGui::Button("Update Header") && g_name[0]) {
        undo_save();
        char old[256];
        snprintf(old, sizeof old, "%s", g_bdb_header);
        char nm[64]; int ww, wh, md, nm2, np, no2;
        if (sscanf(old, "%63s %d %d %d %d %d %d",
                   nm, &ww, &wh, &md, &nm2, &np, &no2) >= 7) {
            snprintf(g_bdb_header, sizeof g_bdb_header,
                     "%s %d %d %d %d %d %d", nm, ww, wh, md,
                     g_bdb_num_modules, g_n_pals, g_no);
        }
    }

    if (!g_simple_mode && g_have_bdb && g_bdb_header[0]) {
        char nm2[64] = ""; int ww2=0, wh2=0, md2=255, nm_n=0, np2=0, no2=0;
        if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
                   nm2, &ww2, &wh2, &md2, &nm_n, &np2, &no2) >= 7) {
            ImGui::SetNextItemWidth(160);
            ImGui::InputText("Stage Name##stagename", nm2, sizeof nm2,
                             ImGuiInputTextFlags_CharsNoBlank);
            if (ImGui::IsItemActivated()) undo_save();
            if (ImGui::IsItemDeactivatedAfterEdit() && nm2[0]) {
                snprintf(g_name, sizeof g_name, "%s", nm2);
                snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d %d",
                         nm2, ww2, wh2, md2, nm_n, np2, no2);
                g_dirty = 1;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The internal stage name written into the BDB header (separate from the filename).\nMust match whatever name BGND.ASM's stage table refers to this stage by.");

            int ww_orig = ww2, wh_orig = wh2;
            ImGui::SetNextItemWidth(80); ImGui::InputInt("World W##ww", &ww2);
            if (ImGui::IsItemActivated()) undo_save();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::InputInt("H##wh",       &wh2);
            if (ImGui::IsItemActivated()) undo_save();
            if (ww2 != ww_orig || wh2 != wh_orig) {
                if (ww2 < 1) ww2 = 1;
                if (wh2 < 1) wh2 = 1;
                snprintf(g_bdb_header, sizeof g_bdb_header,
                         "%s %d %d %d %d %d %d",
                         nm2, ww2, wh2, md2, nm_n, np2, no2);
                g_dirty = 1;
            }
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("LOAD2 Module Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
        int assigned_total = 0;
        for (int m = 0; m < g_bdb_num_modules; m++)
            assigned_total += module_collect_stats(m, NULL, NULL, NULL);
        int outside = mk2_first_unassigned_object() >= 0 ? 1 : 0;
        if (outside) {
            int n = 0;
            for (int i = 0; i < g_no; i++) {
                Img *im = img_find(g_obj[i].ii);
                if (im && assign_module(g_obj[i].depth, g_obj[i].sy, im->w, im->h) < 0)
                    n++;
            }
            outside = n;
        }
        ImGui::TextWrapped("LOAD2 assigns each object to the first module whose inclusive rectangle fully contains the sprite image. Parallax/floor choice comes from the object's Layer.");
        ImGui::Text("Assigned: %d / %d objects   Outside: %d   Modules: %d / %d",
                    assigned_total, g_no, outside, g_bdb_num_modules, MK2_LOAD2_MAX_MODULES);
        if (outside > 0) {
            if (ImGui::Button("Select Outside", ImVec2(130, 0))) {
                int n = mk2_select_unassigned_objects();
                char msg[96];
                snprintf(msg, sizeof msg, "Selected %d outside-module object(s)", n);
                stage_set_toast(msg);
            }
            ImGui::SameLine();
            if (ImGui::Button("Include Outside", ImVec2(140, 0))) {
                int n = mk2_include_unassigned_objects_in_modules();
                char msg[128];
                snprintf(msg, sizeof msg,
                     n ? "Expanded %d module bound(s)" : "No module bounds needed expansion",
                     n);
                stage_set_toast(msg);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Outside", ImVec2(130, 0))) {
                int n = mk2_delete_unassigned_objects();
                mk2_toast_outside_delete_result(n);
            }
        }
        if (g_outside_delete_backup_status[0])
            ImGui::TextWrapped("%s", g_outside_delete_backup_status);

        if (ImGui::BeginTable("module_summary", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("name");
            ImGui::TableSetupColumn("bounds");
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn("obj", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("pal", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 112.0f);
            ImGui::TableHeadersRow();
            for (int m = 0; m < g_bdb_num_modules; m++) {
                char name[64] = "";
                int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                int ok = parse_module_bounds(m, name, &x1, &x2, &y1, &y2);
                int pals = 0, layers = 0, first = -1;
                int objects = ok ? module_collect_stats(m, &pals, &layers, &first) : 0;
                ImVec4 pal_col = pals > MK2_RUNTIME_PALETTE_SLOTS
                               ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
                               : (pals > MK2_BG_DYNAMIC_PALETTE_SLOTS
                                  ? ImVec4(1.0f, 0.65f, 0.25f, 1.0f)
                                  : ImVec4(0.75f, 0.9f, 1.0f, 1.0f));
                ImGui::PushID(m);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%d", m);
                ImGui::TableNextColumn();
                if (ok) ImGui::TextUnformatted(name);
                else ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "bad line");
                ImGui::TableNextColumn();
                if (ok) ImGui::Text("%d..%d, %d..%d", x1, x2, y1, y2);
                else ImGui::TextUnformatted("-");
                ImGui::TableNextColumn();
                if (ok) ImGui::Text("%dx%d", x2 - x1 + 1, y2 - y1 + 1);
                else ImGui::TextUnformatted("-");
                ImGui::TableNextColumn(); ImGui::Text("%d", objects);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Layer count in this module: %d", layers);
                ImGui::TableNextColumn();
                ImGui::TextColored(pal_col, "%d", pals);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("MK2 hardware exposes %d simultaneous palette slots; background dynamic budget is tracked separately.",
                                      MK2_RUNTIME_PALETTE_SLOTS);
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Select")) {
                    int n = module_select_objects(m);
                    char msg[96];
                    snprintf(msg, sizeof msg, "Selected %d object(s) in module %d", n, m);
                    stage_set_toast(msg);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("View"))
                    module_center_view(m);
                if (first >= 0 && ImGui::IsItemHovered())
                    ImGui::SetTooltip("First assigned object: %d", first);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    if (!g_simple_mode) {
        ImGui::Separator();
        draw_module_runtime_binding();
    }

    ImGui::Separator();

    for (int i = 0; i < g_bdb_num_modules; i++) {
        ImGui::PushID(i);
        char mod_lbl[48];
        char mn[64] = ""; int mx0=0, mx1=0, my0=0, my1=0;
        sscanf(g_bdb_modules[i], "%63s %d %d %d %d", mn, &mx0, &mx1, &my0, &my1);
        if (g_simple_mode)
            snprintf(mod_lbl, sizeof mod_lbl, "%s  (%d-%d, %d-%d)", mn[0] ? mn : "Region", mx0, mx1, my0, my1);
        else
            snprintf(mod_lbl, sizeof mod_lbl, "Module %d", i);
        bool open = ImGui::TreeNode(mod_lbl);
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(1))
            g_edit_mod_idx = i;
        if (open) {
            if (!g_simple_mode) {
                char buf[256];
                snprintf(buf, sizeof buf, "%s", g_bdb_modules[i]);
                ImGui::InputText("##mod", buf, sizeof buf);
                if (ImGui::IsItemActivated()) module_line_capture_one(i);
                if (ImGui::IsItemEdited()) {
                    if (g_module_line_capture_active < 0)
                        module_line_capture_one(i);
                    if (editor_project_set_module_line(i, buf))
                        g_dirty = 1;
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    module_line_commit("Edit Module");
                draw_module_bounds_size_editor(i, true);
            } else {
                draw_module_bounds_size_editor(i, true);
            }
            if (ImGui::Button("Delete")) {
                undo_save();
                editor_project_delete_module_line(i);
                ImGui::TreePop();
                ImGui::PopID();
                break;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (g_edit_mod_idx >= 0) {
        ImGui::OpenPopup("Edit Module");
        if (ImGui::BeginPopup("Edit Module")) {
            snprintf(g_mod_edit_buf, sizeof g_mod_edit_buf, "%s",
                     g_bdb_modules[g_edit_mod_idx]);
            ImGui::InputText("Module line", g_mod_edit_buf, sizeof g_mod_edit_buf);
            if (ImGui::IsItemActivated()) module_line_capture_one(g_edit_mod_idx);
            if (ImGui::IsItemEdited()) {
                if (g_module_line_capture_active < 0)
                    module_line_capture_one(g_edit_mod_idx);
                if (editor_project_set_module_line(g_edit_mod_idx, g_mod_edit_buf))
                    g_dirty = 1;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                module_line_commit("Edit Module");
            draw_module_bounds_size_editor(g_edit_mod_idx, true);
            if (ImGui::Button("Close"))
                g_edit_mod_idx = -1;
            ImGui::EndPopup();
        } else {
            g_edit_mod_idx = -1;
        }
    }

    ImGui::End();
}
