#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <algorithm>
#include <climits>
#include <stdio.h>
#include <string.h>
#include <vector>

struct GroupBppPaletteColor {
    int old_idx;
    Uint32 color;
    size_t count;
};

struct SmartPaletteUnit {
    int source_pal;
    int module;
    int pack_idx;
    int image_count;
    int object_count;
    int used_colors;
    bool can_apply;
    char reason[96];
    std::vector<int> img_slots;
    std::vector<GroupBppPaletteColor> colors;
};

struct SmartPalettePack {
    int module;
    int count;
    int unit_count;
    int image_count;
    int object_count;
    Uint32 colors[256];
    std::vector<int> source_pals;
};

struct SmartPalettePlan {
    std::vector<SmartPaletteUnit> units;
    std::vector<SmartPalettePack> packs;
    int scoped_objects;
    int scoped_palettes;
    int eligible_units;
    int risky_images;
    int too_large_units;
    int applied_packs;
    int retired_palettes;
    int current_unused_palettes;
    int expected_palette_count;
};

static bool smart_palette_has_int(const std::vector<int> &v, int x)
{
    for (int n : v)
        if (n == x) return true;
    return false;
}

static void smart_palette_module_label(int module, char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    if (!g_smart_pal_module_aware) {
        snprintf(out, outsz, "any");
        return;
    }
    if (module >= 0 && module < g_bdb_num_modules) {
        char name[64] = "";
        if (parse_module_bounds(module, name, NULL, NULL, NULL, NULL) && name[0])
            snprintf(out, outsz, "%d:%s", module, name);
        else
            snprintf(out, outsz, "%d", module);
        return;
    }
    snprintf(out, outsz, "mixed");
}

static void smart_palette_image_ref_info(const Img *im, int *out_refs,
                                         int *out_first_pal, bool *out_mixed)
{
    if (out_refs) *out_refs = 0;
    if (out_first_pal) *out_first_pal = -1;
    if (out_mixed) *out_mixed = false;
    if (!im) return;

    int refs = 0;
    int first_pal = -1;
    bool mixed = false;
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].ii != im->idx) continue;
        int pal = object_palette_for_image(&g_obj[oi], im);
        if (pal < 0 || pal >= g_n_pals) continue;
        if (first_pal < 0) first_pal = pal;
        else if (pal != first_pal) mixed = true;
        refs++;
    }
    if (out_refs) *out_refs = refs;
    if (out_first_pal) *out_first_pal = first_pal;
    if (out_mixed) *out_mixed = mixed;
}

static int smart_palette_image_module(const Img *im, int source_pal)
{
    if (!im) return -1;
    int module = -2;
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].ii != im->idx) continue;
        int pal = object_palette_for_image(&g_obj[oi], im);
        if (pal != source_pal) continue;
        int m = assign_module(g_obj[oi].depth, g_obj[oi].sy, im->w, im->h);
        if (module == -2) module = m;
        else if (module != m) return -1;
    }
    return module == -2 ? -1 : module;
}

static void smart_palette_add_color(SmartPaletteUnit *unit, int old_idx,
                                    Uint32 color, size_t count)
{
    if (!unit || old_idx <= 0 || old_idx >= 256 || count == 0) return;
    for (GroupBppPaletteColor &c : unit->colors) {
        if (c.old_idx == old_idx) {
            c.count += count;
            return;
        }
    }
    GroupBppPaletteColor c;
    c.old_idx = old_idx;
    c.color = color;
    c.count = count;
    unit->colors.push_back(c);
}

static int smart_palette_distinct_color_count(const std::vector<GroupBppPaletteColor> &colors)
{
    Uint32 tmp[256];
    memset(tmp, 0, sizeof tmp);
    int count = 1;
    for (const GroupBppPaletteColor &c : colors) {
        if (group_bpp_color_index(tmp, count, c.color) >= 0) continue;
        if (count >= 256) return 255;
        tmp[count++] = c.color;
    }
    return count - 1;
}

static int smart_palette_find_unit(std::vector<SmartPaletteUnit> &units,
                                   int source_pal, int module)
{
    for (size_t i = 0; i < units.size(); i++)
        if (units[i].source_pal == source_pal && units[i].module == module)
            return (int)i;
    SmartPaletteUnit u;
    u.source_pal = source_pal;
    u.module = module;
    u.pack_idx = -1;
    u.image_count = 0;
    u.object_count = 0;
    u.used_colors = 0;
    u.can_apply = true;
    snprintf(u.reason, sizeof u.reason, "ready");
    units.push_back(u);
    return (int)units.size() - 1;
}

static int smart_palette_pack_extra_colors(const SmartPalettePack &pack,
                                           const SmartPaletteUnit &unit,
                                           int *out_union)
{
    int extra = 0;
    int union_count = pack.count;
    for (const GroupBppPaletteColor &c : unit.colors) {
        if (group_bpp_color_index(pack.colors, union_count, c.color) >= 0)
            continue;
        extra++;
        union_count++;
    }
    if (out_union) *out_union = union_count;
    return extra;
}

static void smart_palette_pack_add_unit(SmartPalettePack *pack,
                                        const SmartPaletteUnit &unit)
{
    if (!pack) return;
    for (const GroupBppPaletteColor &c : unit.colors) {
        if (group_bpp_color_index(pack->colors, pack->count, c.color) >= 0)
            continue;
        if (pack->count < 256)
            pack->colors[pack->count++] = c.color;
    }
    pack->unit_count++;
    pack->image_count += unit.image_count;
    pack->object_count += unit.object_count;
    if (!smart_palette_has_int(pack->source_pals, unit.source_pal))
        pack->source_pals.push_back(unit.source_pal);
}

static bool smart_palette_pack_is_applied(const SmartPalettePack &pack)
{
    return pack.count > 1 && (int)pack.source_pals.size() >= 2;
}

static void build_smart_palette_plan(SmartPalettePlan &plan)
{
    plan.units.clear();
    plan.packs.clear();
    plan.scoped_objects = 0;
    plan.scoped_palettes = 0;
    plan.eligible_units = 0;
    plan.risky_images = 0;
    plan.too_large_units = 0;
    plan.applied_packs = 0;
    plan.retired_palettes = 0;
    plan.current_unused_palettes = 0;
    plan.expected_palette_count = g_n_pals;

    int target = g_smart_pal_target_colors;
    if (target < 1) target = 1;
    if (target > 255) target = 255;
    int limit_count = target + 1;
    if (limit_count > 256) limit_count = 256;

    const int image_cap = editor_project_image_capacity();
    const int palette_cap = editor_project_palette_capacity();
    std::vector<unsigned char> scope_pal((size_t)(palette_cap > 0 ? palette_cap : 0), 0);
    bool use_selection = g_smart_pal_selected_only && selected_count() > 0;
    for (int oi = 0; oi < g_no; oi++) {
        if (use_selection && !g_sel_flags[oi]) continue;
        Img *im = img_find(g_obj[oi].ii);
        if (!im || !im->pix) continue;
        int pal = object_palette_for_image(&g_obj[oi], im);
        if (pal < 0 || pal >= g_n_pals || pal >= palette_cap) continue;
        if (!scope_pal[(size_t)pal]) {
            scope_pal[(size_t)pal] = 1;
            plan.scoped_palettes++;
        }
        plan.scoped_objects++;
    }
    if (!use_selection && plan.scoped_palettes == 0) {
        for (int p = 0; p < g_n_pals && p < palette_cap; p++) {
            scope_pal[(size_t)p] = 1;
            plan.scoped_palettes++;
        }
    }

    for (int ii = 0; ii < g_ni; ii++) {
        Img *im = &g_img[ii];
        if (!im->pix || im->w <= 0 || im->h <= 0) continue;

        int refs = 0;
        int source_pal = -1;
        bool mixed = false;
        smart_palette_image_ref_info(im, &refs, &source_pal, &mixed);
        if (refs == 0) {
            if (!g_smart_pal_include_unused_images) continue;
            source_pal = im->pal_idx;
        }
        if (source_pal < 0 || source_pal >= g_n_pals || source_pal >= palette_cap) continue;
        if (!scope_pal[(size_t)source_pal]) continue;
        if (mixed) {
            plan.risky_images++;
            continue;
        }

        int module = g_smart_pal_module_aware ? smart_palette_image_module(im, source_pal) : -1;
        int ui = smart_palette_find_unit(plan.units, source_pal, module);
        SmartPaletteUnit &unit = plan.units[(size_t)ui];
        unit.img_slots.push_back(ii);
        unit.image_count++;
        unit.object_count += refs;

        size_t use_count[256];
        memset(use_count, 0, sizeof use_count);
        size_t n = (size_t)im->w * (size_t)im->h;
        for (size_t k = 0; k < n; k++) {
            int v = im->pix[k];
            if (v > 0) use_count[v]++;
        }
        for (int v = 1; v < 256; v++)
            if (use_count[v])
                smart_palette_add_color(&unit, v, palette_argb_at(source_pal, v), use_count[v]);
    }

    for (SmartPaletteUnit &unit : plan.units) {
        unit.used_colors = smart_palette_distinct_color_count(unit.colors);
        if (unit.used_colors <= 0) {
            unit.can_apply = false;
            snprintf(unit.reason, sizeof unit.reason, "no visible pixels");
        } else if (unit.used_colors + 1 > limit_count) {
            unit.can_apply = false;
            snprintf(unit.reason, sizeof unit.reason, "%d colors exceed target", unit.used_colors);
            plan.too_large_units++;
        } else {
            plan.eligible_units++;
        }
    }

    std::vector<int> order;
    for (int i = 0; i < (int)plan.units.size(); i++)
        if (plan.units[(size_t)i].can_apply)
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const SmartPaletteUnit &ua = plan.units[(size_t)a];
        const SmartPaletteUnit &ub = plan.units[(size_t)b];
        if (ua.module != ub.module) return ua.module < ub.module;
        if (ua.used_colors != ub.used_colors) return ua.used_colors > ub.used_colors;
        if (ua.object_count != ub.object_count) return ua.object_count > ub.object_count;
        return ua.source_pal < ub.source_pal;
    });

    for (int ui : order) {
        SmartPaletteUnit &unit = plan.units[(size_t)ui];
        int best = -1;
        int best_extra = INT_MAX;
        int best_union = INT_MAX;
        for (int pi = 0; pi < (int)plan.packs.size(); pi++) {
            SmartPalettePack &pack = plan.packs[(size_t)pi];
            if (g_smart_pal_module_aware && pack.module != unit.module) continue;
            int union_count = 0;
            int extra = smart_palette_pack_extra_colors(pack, unit, &union_count);
            if (union_count > limit_count) continue;
            if (extra < best_extra || (extra == best_extra && union_count < best_union)) {
                best = pi;
                best_extra = extra;
                best_union = union_count;
            }
        }
        if (best < 0) {
            SmartPalettePack pack;
            pack.module = unit.module;
            pack.count = 1;
            pack.unit_count = 0;
            pack.image_count = 0;
            pack.object_count = 0;
            memset(pack.colors, 0, sizeof pack.colors);
            plan.packs.push_back(pack);
            best = (int)plan.packs.size() - 1;
        }
        unit.pack_idx = best;
        smart_palette_pack_add_unit(&plan.packs[(size_t)best], unit);
    }

    for (const SmartPalettePack &pack : plan.packs)
        if (smart_palette_pack_is_applied(pack))
            plan.applied_packs++;

    std::vector<unsigned char> covered_img((size_t)(image_cap > 0 ? image_cap : 0), 0);
    std::vector<unsigned char> touched_pal((size_t)(palette_cap > 0 ? palette_cap : 0), 0);
    for (const SmartPaletteUnit &unit : plan.units) {
        if (unit.pack_idx < 0 || unit.pack_idx >= (int)plan.packs.size()) continue;
        if (!smart_palette_pack_is_applied(plan.packs[(size_t)unit.pack_idx])) continue;
        if (unit.source_pal >= 0 && unit.source_pal < palette_cap)
            touched_pal[(size_t)unit.source_pal] = 1;
        for (int ii : unit.img_slots)
            if (ii >= 0 && ii < image_cap) covered_img[(size_t)ii] = 1;
    }

    for (int p = 0; p < g_n_pals; p++) {
        bool has_ref = false;
        bool remains = false;
        for (int ii = 0; ii < g_ni; ii++) {
            if (g_img[ii].pal_idx != p) continue;
            has_ref = true;
            if (ii < 0 || ii >= image_cap || !covered_img[(size_t)ii]) remains = true;
        }
        for (int oi = 0; oi < g_no; oi++) {
            if (g_obj[oi].fl != p) continue;
            has_ref = true;
            Img *im = img_find(g_obj[oi].ii);
            int img_slot = im ? (int)(im - g_img) : -1;
            if (img_slot < 0 || img_slot >= image_cap || !covered_img[(size_t)img_slot])
                remains = true;
        }
        if (!has_ref) {
            plan.current_unused_palettes++;
        } else if (p >= 0 && p < palette_cap && touched_pal[(size_t)p] && !remains) {
            plan.retired_palettes++;
        }
    }

    plan.expected_palette_count = g_n_pals + plan.applied_packs - plan.retired_palettes;
    if (g_smart_pal_remove_unused)
        plan.expected_palette_count -= plan.current_unused_palettes;
    if (plan.expected_palette_count < 0)
        plan.expected_palette_count = 0;
}

static int apply_smart_palette_plan(const SmartPalettePlan &plan)
{
    if (plan.applied_packs <= 0) return 0;
    if (!editor_project_reserve_palettes(g_n_pals + plan.applied_packs)) return -1;

    undo_save_ex("Smart Group Palettes");
    std::vector<int> pack_to_pal(plan.packs.size(), -1);

    int created = 0;
    int remapped_images = 0;
    int updated_objects = 0;
    for (size_t pi = 0; pi < plan.packs.size(); pi++) {
        const SmartPalettePack &pack = plan.packs[pi];
        if (!smart_palette_pack_is_applied(pack)) continue;
        char name[64];
        if (pack.module >= 0)
            snprintf(name, sizeof name, "SGRP_M%02d_%02d", pack.module & 0xFF, created & 0xFF);
        else
            snprintf(name, sizeof name, "SGRP_MX_%02d", created & 0xFF);
        int dst = editor_project_append_palette_slot(name, pack.count, pack.colors);
        if (dst < 0) return -1;
        pack_to_pal[pi] = dst;
        created++;
    }

    for (const SmartPaletteUnit &unit : plan.units) {
        if (unit.pack_idx < 0 || unit.pack_idx >= (int)plan.packs.size()) continue;
        int dst = pack_to_pal[(size_t)unit.pack_idx];
        if (dst < 0) continue;
        const SmartPalettePack &pack = plan.packs[(size_t)unit.pack_idx];
        int map[256];
        memset(map, 0, sizeof map);
        for (const GroupBppPaletteColor &c : unit.colors) {
            int idx = group_bpp_color_index(pack.colors, pack.count, c.color);
            if (idx > 0 && c.old_idx > 0 && c.old_idx < 256)
                map[c.old_idx] = idx;
        }

        for (int img_slot : unit.img_slots) {
            if (img_slot < 0 || img_slot >= g_ni) continue;
            Img *im = &g_img[img_slot];
            if (!im->pix) continue;
            size_t n = (size_t)im->w * (size_t)im->h;
            for (size_t k = 0; k < n; k++) {
                Uint8 v = im->pix[k];
                if (v > 0 && map[v] > 0)
                    im->pix[k] = (Uint8)map[v];
            }
            im->pal_idx = dst;
            remapped_images++;
            for (int oi = 0; oi < g_no; oi++) {
                if (g_obj[oi].ii != im->idx) continue;
                if (g_obj[oi].fl == unit.source_pal) {
                    g_obj[oi].fl = dst;
                    updated_objects++;
                }
            }
        }
    }

    int removed = 0;
    if (g_smart_pal_remove_unused)
        removed = remove_unused_palettes_impl(false);

    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;
    g_mk2_palette_sync_dirty = true;
    if (g_sel_pal >= g_n_pals) g_sel_pal = g_n_pals > 0 ? g_n_pals - 1 : 0;

    snprintf(g_smart_pal_status, sizeof g_smart_pal_status,
             "Created %d shared palette(s), remapped %d image(s), updated %d object(s), removed %d unused.",
             created, remapped_images, updated_objects, removed);
    stage_set_toast(g_smart_pal_status);
    return created;
}

void draw_mk2_smart_palette_grouper(void)
{
    if (g_n_pals <= 1) {
        ImGui::TextDisabled("Need at least two palettes.");
        return;
    }
    if (g_smart_pal_target_colors < 1) g_smart_pal_target_colors = 1;
    if (g_smart_pal_target_colors > 255) g_smart_pal_target_colors = 255;

    SmartPalettePlan plan;
    build_smart_palette_plan(plan);

    ImGui::Text("Smart Palette Grouper");
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Target nonzero colors", &g_smart_pal_target_colors, 1, 16)) {
        if (g_smart_pal_target_colors < 1) g_smart_pal_target_colors = 1;
        if (g_smart_pal_target_colors > 255) g_smart_pal_target_colors = 255;
    }
    ImGui::Checkbox("Seed from selected objects", &g_smart_pal_selected_only);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When objects are selected, only palettes touched by that selection seed the grouping pass.");
    ImGui::SameLine();
    ImGui::Checkbox("Keep LOAD2 modules separate", &g_smart_pal_module_aware);
    ImGui::Checkbox("Include unused images", &g_smart_pal_include_unused_images);
    ImGui::SameLine();
    ImGui::Checkbox("Remove unused after apply", &g_smart_pal_remove_unused);

    ImGui::Text("Palettes: %d -> %d   groups: %d   retiring: %d",
                g_n_pals, plan.expected_palette_count,
                plan.applied_packs, plan.retired_palettes + (g_smart_pal_remove_unused ? plan.current_unused_palettes : 0));
    ImGui::Text("Scope: %d object(s), %d palette(s), %d eligible group seed(s)",
                plan.scoped_objects, plan.scoped_palettes, plan.eligible_units);
    if (plan.risky_images > 0 || plan.too_large_units > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                           "Skipped: %d mixed-palette image(s), %d over target.",
                           plan.risky_images, plan.too_large_units);
    }

    bool can_apply = plan.applied_packs > 0 &&
                     plan.expected_palette_count < g_n_pals;
    if (plan.applied_packs > 0 && plan.expected_palette_count >= g_n_pals)
        ImGui::TextDisabled("No net palette-count win with the current scope.");

    if (!can_apply) ImGui::BeginDisabled();
    if (ImGui::Button("Apply Smart Palette Groups", ImVec2(-1, 0))) {
        int rc = apply_smart_palette_plan(plan);
        if (rc < 0) stage_set_toast("Smart grouping refused: not enough palette slots");
        else if (rc == 0) stage_set_toast("No safe palette groups found");
    }
    if (!can_apply) ImGui::EndDisabled();

    if (g_smart_pal_status[0])
        ImGui::TextWrapped("%s", g_smart_pal_status);

    if (ImGui::BeginTable("smart_palette_groups", 7,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 150.0f)))
    {
        ImGui::TableSetupColumn("grp", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("module", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("src", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("colors", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("imgs", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("objs", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("state");
        ImGui::TableHeadersRow();
        for (size_t pi = 0; pi < plan.packs.size(); pi++) {
            const SmartPalettePack &pack = plan.packs[pi];
            if (pack.unit_count <= 0) continue;
            char module[64];
            smart_palette_module_label(pack.module, module, sizeof module);
            bool applied = smart_palette_pack_is_applied(pack);
            ImGui::TableNextRow();
            if (applied)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(45, 105, 55, 55));
            ImGui::TableNextColumn(); ImGui::Text("%d", (int)pi);
            ImGui::TableNextColumn(); ImGui::Text("%s", module);
            ImGui::TableNextColumn(); ImGui::Text("%d", (int)pack.source_pals.size());
            ImGui::TableNextColumn(); ImGui::Text("%d", pack.count - 1);
            ImGui::TableNextColumn(); ImGui::Text("%d", pack.image_count);
            ImGui::TableNextColumn(); ImGui::Text("%d", pack.object_count);
            ImGui::TableNextColumn();
            if (applied)
                ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "will create shared palette");
            else
                ImGui::TextDisabled("single source");
        }
        ImGui::EndTable();
    }

    if (ImGui::BeginTable("smart_palette_units", 7,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 135.0f)))
    {
        ImGui::TableSetupColumn("pal", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("grp", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("module", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("colors", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("imgs", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("objs", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("state");
        ImGui::TableHeadersRow();
        int shown = 0;
        for (const SmartPaletteUnit &unit : plan.units) {
            if (shown++ >= 160) break;
            char module[64];
            smart_palette_module_label(unit.module, module, sizeof module);
            bool applied = unit.pack_idx >= 0 && unit.pack_idx < (int)plan.packs.size() &&
                           smart_palette_pack_is_applied(plan.packs[(size_t)unit.pack_idx]);
            ImGui::TableNextRow();
            if (applied)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(45, 105, 55, 55));
            ImGui::TableNextColumn(); ImGui::Text("%d", unit.source_pal);
            ImGui::TableNextColumn();
            if (unit.pack_idx >= 0) ImGui::Text("%d", unit.pack_idx);
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn(); ImGui::Text("%s", module);
            ImGui::TableNextColumn(); ImGui::Text("%d", unit.used_colors);
            ImGui::TableNextColumn(); ImGui::Text("%d", unit.image_count);
            ImGui::TableNextColumn(); ImGui::Text("%d", unit.object_count);
            ImGui::TableNextColumn();
            if (applied)
                ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "grouped");
            else if (unit.can_apply)
                ImGui::TextDisabled("no partner");
            else
                ImGui::TextDisabled("%s", unit.reason);
        }
        ImGui::EndTable();
    }
}
