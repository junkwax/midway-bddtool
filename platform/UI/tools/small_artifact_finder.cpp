#include "bg_editor_globals.h"
#include "undo_manager.h"
#include "imgui.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

static int g_artifact_max_w = 8;
static int g_artifact_max_h = 8;
static int g_artifact_max_nonzero = 16;
static bool g_artifact_only_split_sources = true;
static bool g_artifact_delete_orphan_images = true;

struct SmallArtifactCandidate {
    int obj_i;
    int img_i;
    int w, h;
    int nonzero;
    int layer;
    char reason[64];
};

static bool text_has_ci(const char *s, const char *needle)
{
    if (!s || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *p = s; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i]) {
            unsigned char a = (unsigned char)p[i];
            unsigned char b = (unsigned char)needle[i];
            if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 32);
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static int image_nonzero_pixel_count(const Img *im)
{
    if (!im || !im->pix || im->w <= 0 || im->h <= 0) return 0;
    int count = 0;
    size_t n = (size_t)im->w * (size_t)im->h;
    for (size_t i = 0; i < n; i++)
        if (im->pix[i] != 0) count++;
    return count;
}

static bool image_looks_split_artifact_source(const Img *im)
{
    if (!im) return false;
    return text_has_ci(im->source, "CHOP") || text_has_ci(im->source, "SPLIT") ||
           text_has_ci(im->label, "CHOP") || text_has_ci(im->label, "SPLIT");
}

static void collect_small_artifact_candidates(std::vector<SmallArtifactCandidate> &out)
{
    out.clear();
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj_hidden[oi]) continue;
        Obj *o = &g_obj[oi];
        Img *im = img_find(o->ii);
        if (!im || !im->pix || im->w <= 0 || im->h <= 0) continue;
        bool split_source = image_looks_split_artifact_source(im);
        int nonzero = image_nonzero_pixel_count(im);
        bool tiny_dims = im->w <= g_artifact_max_w && im->h <= g_artifact_max_h;
        bool tiny_pixels = nonzero <= g_artifact_max_nonzero;
        if (g_artifact_only_split_sources && !split_source) continue;
        if (!tiny_dims && !tiny_pixels) continue;

        SmallArtifactCandidate c;
        memset(&c, 0, sizeof c);
        c.obj_i = oi;
        c.img_i = (int)(im - g_img);
        c.w = im->w;
        c.h = im->h;
        c.nonzero = nonzero;
        c.layer = (o->wx >> 8) & 0xFF;
        snprintf(c.reason, sizeof c.reason, "%s%s",
                 split_source ? "split " : "",
                 tiny_pixels ? "tiny pixels" : "tiny dims");
        out.push_back(c);
    }
    std::stable_sort(out.begin(), out.end(), [](const SmallArtifactCandidate &a,
                                                const SmallArtifactCandidate &b) {
        if (a.nonzero != b.nonzero) return a.nonzero < b.nonzero;
        int aa = a.w * a.h;
        int ba = b.w * b.h;
        if (aa != ba) return aa < ba;
        return a.obj_i < b.obj_i;
    });
}

static int select_small_artifact_candidates(const std::vector<SmallArtifactCandidate> &cands)
{
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return 0;
    editor_project_clear_selection();
    g_hl_obj = -1;
    int selected = 0;
    for (size_t i = 0; i < cands.size(); i++) {
        int oi = cands[i].obj_i;
        if (oi < 0 || oi >= g_no) continue;
        g_sel_flags[oi] = 1;
        if (g_hl_obj < 0) g_hl_obj = oi;
        selected++;
    }
    return selected;
}

static int delete_small_artifact_candidates(const std::vector<SmallArtifactCandidate> &cands)
{
    if (cands.empty()) return 0;
    int object_cap = editor_project_object_capacity();
    int image_cap = editor_project_image_capacity();
    if (object_cap <= 0 || image_cap <= 0) return 0;
    std::vector<unsigned char> remove((size_t)object_cap, 0);
    std::vector<unsigned char> maybe_delete_img((size_t)image_cap, 0);
    int marked = 0;
    for (size_t i = 0; i < cands.size(); i++) {
        int oi = cands[i].obj_i;
        if (oi < 0 || oi >= g_no || remove[oi]) continue;
        remove[oi] = 1;
        if (cands[i].img_i >= 0 && cands[i].img_i < g_ni)
            maybe_delete_img[cands[i].img_i] = 1;
        marked++;
    }
    if (marked <= 0) return 0;

    undo_save_ex("Delete Small Artifacts");
    for (int oi = g_no - 1; oi >= 0; oi--)
        if (remove[oi]) mk2_delete_object_preserve_order(oi);
    int removed_images = 0;
    if (g_artifact_delete_orphan_images) {
        for (int ii = g_ni - 1; ii >= 0; ii--)
            if (maybe_delete_img[ii])
                removed_images += delete_image_slot_if_unused(ii);
    }
    editor_project_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;

    char msg[128];
    snprintf(msg, sizeof msg, "Deleted %d artifact object(s), %d orphan image(s)",
             marked, removed_images);
    stage_set_toast(msg);
    return marked;
}

void draw_mk2_small_artifact_finder(void)
{
    ImGui::Text("Small Artifact Finder");
    ImGui::TextDisabled("Flags tiny split/chop placements that may be leftovers after splitting a sprite.");
    ImGui::Checkbox("Only split/chop sources", &g_artifact_only_split_sources);
    ImGui::SameLine();
    ImGui::Checkbox("Delete orphan images", &g_artifact_delete_orphan_images);
    ImGui::SetNextItemWidth(70);
    ImGui::InputInt("Max W", &g_artifact_max_w);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputInt("Max H", &g_artifact_max_h);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("Max nonzero", &g_artifact_max_nonzero);
    if (g_artifact_max_w < 1) g_artifact_max_w = 1;
    if (g_artifact_max_h < 1) g_artifact_max_h = 1;
    if (g_artifact_max_nonzero < 1) g_artifact_max_nonzero = 1;

    std::vector<SmallArtifactCandidate> cands;
    collect_small_artifact_candidates(cands);
    ImGui::Text("%d candidate object(s)", (int)cands.size());
    if (!cands.empty()) {
        if (ImGui::SmallButton("Select Listed")) {
            int n = select_small_artifact_candidates(cands);
            char msg[96];
            snprintf(msg, sizeof msg, "Selected %d artifact candidate(s)", n);
            stage_set_toast(msg);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete Listed Objects"))
            delete_small_artifact_candidates(cands);
    }

    if (ImGui::BeginTable("small_artifact_table", 7,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 180)))
    {
        ImGui::TableSetupColumn("obj", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("img", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("nz", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("lyr", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("why");
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();
        int shown = 0;
        for (size_t i = 0; i < cands.size() && shown < 64; i++, shown++) {
            SmallArtifactCandidate *c = &cands[i];
            Img *im = (c->img_i >= 0 && c->img_i < g_ni) ? &g_img[c->img_i] : NULL;
            ImGui::PushID(c->obj_i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", c->obj_i);
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", im ? im->idx : -1);
            ImGui::TableNextColumn(); ImGui::Text("%dx%d", c->w, c->h);
            ImGui::TableNextColumn(); ImGui::Text("%d", c->nonzero);
            ImGui::TableNextColumn(); ImGui::Text("%02X", c->layer);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(c->reason);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Sel")) {
                int object_cap = editor_project_object_capacity();
                if (object_cap > 0)
                    editor_project_clear_selection();
                g_sel_flags[c->obj_i] = 1;
                g_hl_obj = c->obj_i;
                center_view_on_object(c->obj_i);
            }
            if (im && ImGui::IsItemHovered()) {
                SDL_Texture *tex = editor_texture_at(c->img_i);
                if (tex) {
                    ImGui::BeginTooltip();
                    float sc = 96.0f / (float)(im->w > im->h ? im->w : im->h);
                    if (sc > 8.0f) sc = 8.0f;
                    if (sc < 1.0f) sc = 1.0f;
                    draw_editor_texture_transparent(tex, im->w * sc, im->h * sc);
                    ImGui::EndTooltip();
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Del")) {
                std::vector<SmallArtifactCandidate> one;
                one.push_back(*c);
                delete_small_artifact_candidates(one);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
