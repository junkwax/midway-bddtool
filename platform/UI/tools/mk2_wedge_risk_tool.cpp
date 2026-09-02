#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>
#include <vector>

/* ----------------------------------------------------------------------------
 * LOAD2 zero-compression efficiency scanner.
 *
 * LOAD2 stores each sprite row as a leading and a trailing transparent run plus
 * the visible span between them. Each run is nibble(0..15) * a multiplier, and
 * leading and trailing get their OWN multiplier from {1,2,4,8}, chosen to
 * minimise the total un-encoded remainder across the whole image
 * (zcom_analysis, doc/load2/zcom.c).
 *
 * This scanner flags sprites whose runs no single multiplier expresses exactly.
 * That is a PACKING cost, not corruption: the encoder floors each run
 * (`zlc = zlc / lm; x0 = zlc * lm;`), so the stored span always starts at or
 * before the first visible pixel and ends at or after the last. The remainder
 * is emitted as literal transparent pixels. Nothing shifts and no art is lost -
 * the image just costs more bits than it could. If compression does not pay at
 * all, LOAD2 stores the sprite raw, which is equally lossless.
 *
 * This was previously documented here as a "missing triangle wedge" that ate
 * art in game. It is not: 424 of the 1081 shipped BDD images fail this test,
 * including 8 of the 13 in retail TOWER2, and the retail game renders fine.
 * Treat a hit as "this sprite could pack tighter", never as a defect, and do
 * not gate a build on it.
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
    ImGui::Text("Sprite Packing Efficiency (LOAD2 zero-compression)");
    ImGui::TextWrapped(
        "Finds sprites whose transparent edges LOAD2 cannot encode exactly, so it "
        "pads them with literal transparent pixels. This is a ROM-size cost only - "
        "the encoding floors each run and is lossless, so nothing shifts and no art "
        "is lost in game. 39%% of the shipped sprites are in this state. Tight-"
        "trimming a diagonal margin makes the sprite pack smaller.");

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
                           "Every sprite packs exactly (%d scanned).", scanned);
        return;
    }
    ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f),
                       "%d of %d sprite(s) pad out to a run multiplier (size only).",
                       at_risk, scanned);

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
