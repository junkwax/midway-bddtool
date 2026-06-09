#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>
#include <vector>

/* ----------------------------------------------------------------------------
 * MK2 "missing triangle wedge" risk scanner.
 *
 * The editor stores flat, uncompressed sprite pixels; the external LOAD2
 * toolchain re-compresses each row as a leading/trailing transparent run plus a
 * visible span. Each run is encoded as nibble(0..15) * multiplier, where a
 * SINGLE multiplier (1,2,4,8) is chosen per image. When a sprite has a diagonal
 * or curved transparent edge, a row's true transparent run may not be an exact
 * multiple of any usable multiplier (or may exceed 15*multiplier). LOAD2 then
 * rounds it, so visible pixels shift progressively row-over-row and a triangular
 * wedge of art goes missing in game (but renders fine in this editor).
 *
 * We can't change LOAD2's compressor from here, but we have the flat pixels, so
 * we can flag exactly which sprites it cannot encode cleanly.
 * ------------------------------------------------------------------------- */

static const int kWedgeMultipliers[4] = { 1, 2, 4, 8 };
static const int kWedgeMaxNibble = 15;

/* True if every run value can be expressed as nibble(0..15) * one shared
   multiplier from {1,2,4,8}. Empty input is trivially encodable. */
static bool wedge_runs_encodable(const std::vector<int> &runs)
{
    for (int m = 0; m < 4; m++) {
        int mult = kWedgeMultipliers[m];
        bool ok = true;
        for (size_t i = 0; i < runs.size(); i++) {
            int v = runs[i];
            if ((v % mult) != 0 || (v / mult) > kWedgeMaxNibble) { ok = false; break; }
        }
        if (ok) return true;
    }
    return runs.empty();
}

/* Analyze one image. Returns true if it is at wedge risk and fills the reason. */
static bool wedge_image_at_risk(const Img *im, char *reason, size_t reason_sz,
                                int *out_max_leading, int *out_max_trailing)
{
    if (out_max_leading) *out_max_leading = 0;
    if (out_max_trailing) *out_max_trailing = 0;
    if (!im || !im->pix || im->w <= 0 || im->h <= 0)
        return false;

    std::vector<int> leading;
    std::vector<int> trailing;
    leading.reserve((size_t)im->h);
    trailing.reserve((size_t)im->h);

    int max_lead = 0, max_trail = 0;
    for (int y = 0; y < im->h; y++) {
        const Uint8 *row = im->pix + (size_t)y * (size_t)im->w;
        int first = -1, last = -1;
        for (int x = 0; x < im->w; x++) {
            if (row[x] != 0) { if (first < 0) first = x; last = x; }
        }
        if (first < 0)
            continue; /* fully transparent row renders blank either way */
        int lead = first;
        int trail = im->w - 1 - last;
        leading.push_back(lead);
        trailing.push_back(trail);
        if (lead > max_lead) max_lead = lead;
        if (trail > max_trail) max_trail = trail;
    }

    if (out_max_leading) *out_max_leading = max_lead;
    if (out_max_trailing) *out_max_trailing = max_trail;

    bool lead_ok = wedge_runs_encodable(leading);
    bool trail_ok = wedge_runs_encodable(trailing);
    if (lead_ok && trail_ok)
        return false;

    if (reason && reason_sz) {
        if (!lead_ok && !trail_ok)
            snprintf(reason, reason_sz, "left & right transparent runs (max %d / %d px) not row-encodable",
                     max_lead, max_trail);
        else if (!lead_ok)
            snprintf(reason, reason_sz, "left transparent run (max %d px) not row-encodable", max_lead);
        else
            snprintf(reason, reason_sz, "right transparent run (max %d px) not row-encodable", max_trail);
    }
    return true;
}

static void wedge_focus_image(int img_i)
{
    if (img_i < 0 || img_i >= g_ni) return;
    Img *im = &g_img[img_i];
    g_budget_relief_highlight_img_ii = im->idx;
    g_place_tool_img = img_i;
    g_show_images = true;
    int selected = mk2_select_objects_by_image(im->idx);
    if (selected > 0 && g_hl_obj >= 0 && g_hl_obj < g_no) {
        g_view_x = g_obj[g_hl_obj].depth - 160;
        g_view_y = g_obj[g_hl_obj].sy - 96;
        g_view_changed = 1;
    }
}

void draw_mk2_wedge_risk_tool(void)
{
    ImGui::Text("Sprite Wedge Risk (LOAD2 row encoding)");
    ImGui::TextWrapped(
        "Finds sprites whose transparent edges LOAD2 cannot compress cleanly. "
        "These can show a missing triangle wedge in game even though they look "
        "correct here. Tight-trimming the sprite usually removes the diagonal "
        "transparent margin that triggers it.");

    int scanned = 0, at_risk = 0;
    std::vector<int> risky;
    for (int i = 0; i < g_ni; i++) {
        if (!g_img[i].pix) continue;
        scanned++;
        char reason[96];
        if (wedge_image_at_risk(&g_img[i], reason, sizeof reason, NULL, NULL)) {
            at_risk++;
            risky.push_back(i);
        }
    }

    if (at_risk == 0) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
                           "No wedge-prone sprites (%d scanned).", scanned);
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                       "%d of %d sprite(s) at wedge risk.", at_risk, scanned);

    if (ImGui::BeginTable("wedge_risk", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, 220))) {
        ImGui::TableSetupColumn("image", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("why", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("do", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        bool stop = false;
        for (size_t r = 0; r < risky.size() && !stop; r++) {
            int i = risky[r];
            Img *im = &g_img[i];
            char reason[96] = "";
            int ml = 0, mt = 0;
            wedge_image_at_risk(im, reason, sizeof reason, &ml, &mt);

            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char ii_label[24];
            snprintf(ii_label, sizeof ii_label, "0x%02X", im->idx);
            if (ImGui::SmallButton(ii_label))
                wedge_focus_image(i);
            ImGui::TableNextColumn();
            ImGui::Text("%dx%d", im->w, im->h);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(reason);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Focus"))
                wedge_focus_image(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("Trim")) {
                int saved = trim_image_transparent_border(i, true);
                char msg[96];
                snprintf(msg, sizeof msg,
                         saved > 0 ? "Trimmed %d px; re-scan to confirm" : "No trim applied",
                         saved);
                stage_set_toast(msg);
                stop = true; /* indices/list changed; rebuild next frame */
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "If trimming doesn't clear it, the sprite needs a full-rectangle redraw "
        "or a no-compress flag in the LOAD2 build.");
}
