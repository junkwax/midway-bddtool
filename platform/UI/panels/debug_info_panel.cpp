#include "bg_editor_globals.h"
#include "Core/image_lookup.h"
#include "Core/editor_project_storage.h"
#include "imgui.h"

#include <stdio.h>

static void debug_hex_words(const char *label, const Uint16 *data, int count)
{
    if (!data || count <= 0) {
        ImGui::TextDisabled("%s: <none>", label);
        return;
    }
    if (count > 16) count = 16;
    char buf[256];
    int p = snprintf(buf, sizeof buf, "%s: ", label);
    for (int i = 0; i < count && p < (int)sizeof buf - 8; i++)
        p += snprintf(buf + p, sizeof(buf) - (size_t)p, "%04X ", data[i]);
    ImGui::TextUnformatted(buf);
}

/* Raw-field inspector for the currently loaded BDB/BDD — every value here is
 * read straight from the in-memory project state, not re-derived/formatted
 * for display, so it matches what gets written to disk on save. */
void draw_debug_info(void)
{
    if (!g_show_debug_info) return;
    ImGui::SetNextWindowSize(ImVec2(480.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug Info##bdd_debug", &g_show_debug_info)) {
        ImGui::End();
        return;
    }

    if (!g_have_bdb) {
        ImGui::TextDisabled("No BDB loaded.");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("BDB header", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("g_name:        %s", g_name);
        ImGui::Text("g_bdb_path:    %s", g_bdb_path);
        ImGui::TextWrapped("g_bdb_header:  \"%s\"", g_bdb_header);
        char nm[64] = ""; int ww = 0, wh = 0, md = 255, nmods = 0, npals = 0, nobj = 0;
        int fields = sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
                             nm, &ww, &wh, &md, &nmods, &npals, &nobj);
        ImGui::Text("parsed fields: %d", fields);
        ImGui::Text("world_w:       %d", ww);
        ImGui::Text("world_h:       %d", wh);
        ImGui::Text("max_depth:     %d", md);
        ImGui::Text("module_count (header): %d   (live g_bdb_num_modules: %d)", nmods, g_bdb_num_modules);
        ImGui::Text("palette_count (header): %d  (live g_n_pals: %d)", npals, g_n_pals);
        ImGui::Text("object_count (header): %d   (live g_no: %d)", nobj, g_no);
        ImGui::Text("g_ni (images): %d", g_ni);
    }

    if (ImGui::CollapsingHeader("Modules (raw BDB lines)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (g_bdb_num_modules <= 0) {
            ImGui::TextDisabled("(none)");
        } else {
            for (int m = 0; m < g_bdb_num_modules; m++) {
                ImGui::Text("[%d] \"%s\"", m, g_bdb_modules[m]);
            }
        }
    }

    if (ImGui::CollapsingHeader("Selected object", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (g_hl_obj < 0 || g_hl_obj >= g_no) {
            ImGui::TextDisabled("(none selected)");
        } else {
            const Obj *o = &g_obj[g_hl_obj];
            ImGui::Text("index:    %d", g_hl_obj);
            ImGui::Text("order:    %d", o->order);
            ImGui::Text("wx:       0x%04X  (layer/z = 0x%02X, x = %d)",
                        o->wx, (o->wx >> 8) & 0xFF, o->wx & 0xFF);
            ImGui::Text("depth:    %d", o->depth);
            ImGui::Text("sy:       %d", o->sy);
            ImGui::Text("ii:       0x%03X (%d)", o->ii, o->ii);
            ImGui::Text("fl:       %d  (palette index)", o->fl);
            ImGui::Text("hfl:      %d", o->hfl);
            ImGui::Text("vfl:      %d", o->vfl);
            ImGui::Text("selected: %s   locked: %s   hidden: %s",
                        (g_sel_flags && g_sel_flags[g_hl_obj]) ? "yes" : "no",
                        (g_obj_lock && g_obj_lock[g_hl_obj]) ? "yes" : "no",
                        (g_obj_hidden && g_obj_hidden[g_hl_obj]) ? "yes" : "no");
        }
    }

    if (ImGui::CollapsingHeader("Selected object's image", ImGuiTreeNodeFlags_DefaultOpen)) {
        const Img *im = (g_hl_obj >= 0 && g_hl_obj < g_no) ? img_find(g_obj[g_hl_obj].ii) : NULL;
        if (!im) {
            ImGui::TextDisabled("(none)");
        } else {
            ImGui::Text("idx:       0x%03X (%d)", im->idx, im->idx);
            ImGui::Text("w x h:     %d x %d", im->w, im->h);
            ImGui::Text("flags:     0x%04X", im->flags);
            ImGui::Text("pal_idx:   %d", im->pal_idx);
            ImGui::Text("anix/aniy: %d, %d", im->anix, im->aniy);
            ImGui::Text("anix2/aniy2/aniz2: %d, %d, %d", im->anix2, im->aniy2, im->aniz2);
            ImGui::Text("frm:       %d", im->frm);
            ImGui::Text("opals:     0x%04X", im->opals);
            ImGui::Text("pttblnum:  %d", im->pttblnum);
            ImGui::Text("lod_ref:   %d", im->lod_ref);
            ImGui::Text("label:     \"%s\"", im->label);
            ImGui::Text("source:    \"%s\"", im->source);
            ImGui::Text("pix:       %s", im->pix ? "loaded" : "<null>");
        }
    }

    if (ImGui::CollapsingHeader("Selected palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (g_sel_pal < 0 || g_sel_pal >= g_n_pals) {
            ImGui::TextDisabled("(none selected)");
        } else {
            ImGui::Text("index:  %d", g_sel_pal);
            ImGui::Text("name:   \"%s\"", g_pal_name[g_sel_pal]);
            ImGui::Text("count:  %d", g_pal_count[g_sel_pal]);
            Uint16 raw[16] = {0};
            int got = editor_project_get_palette_rgb555_cache(g_sel_pal, raw, 16);
            if (got > 0)
                debug_hex_words("rgb555[0..15]", raw, got);
            else
                ImGui::TextDisabled("rgb555: not cached (re-save or reload to populate)");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("F9 to toggle");
    ImGui::End();
}
