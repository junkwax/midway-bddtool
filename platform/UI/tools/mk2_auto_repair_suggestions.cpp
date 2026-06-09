#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdlib.h>
#include <string.h>

struct ImgRepairProfile {
    int nonzero;
    int area;
    int coverage_pct;
    int max_pixel;
    int avg_r;
    int avg_g;
    int avg_b;
    bool strip;
    bool fill;
    bool blue;
    bool orange;
};

static ImgRepairProfile analyze_repair_profile(const Img *im)
{
    ImgRepairProfile p;
    memset(&p, 0, sizeof p);
    if (!im || !im->pix || im->w <= 0 || im->h <= 0) return p;
    p.area = im->w * im->h;
    p.max_pixel = image_max_pixel(im);
    Uint64 sr = 0, sg = 0, sb = 0;
    const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? g_pals[im->pal_idx] : NULL;
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            Uint8 v = im->pix[y * im->w + x];
            if (v == 0) continue;
            p.nonzero++;
            if (pal) {
                Uint32 c = pal[v];
                sr += (c >> 16) & 0xFF;
                sg += (c >> 8) & 0xFF;
                sb += c & 0xFF;
            }
        }
    }
    if (p.nonzero > 0 && pal) {
        p.avg_r = (int)(sr / (Uint64)p.nonzero);
        p.avg_g = (int)(sg / (Uint64)p.nonzero);
        p.avg_b = (int)(sb / (Uint64)p.nonzero);
    }
    p.coverage_pct = p.area > 0 ? (p.nonzero * 100) / p.area : 0;
    p.strip = (im->w >= im->h * 3) || (im->h >= im->w * 3);
    p.fill = p.area >= 1024 && p.coverage_pct >= 55;
    p.blue = p.avg_b > p.avg_r + 24 && p.avg_b > p.avg_g + 8;
    p.orange = p.avg_r > p.avg_b + 32 && p.avg_g > p.avg_b + 8;
    return p;
}

static int mk2_layer_behind(int layer)
{
    if (layer > 0x43) return 0x43;
    if (layer > 0x40) return 0x40;
    if (layer > 0x3C) return 0x3C;
    return 0x32;
}

static void repair_reason(const Img *im, const ImgRepairProfile *p, char *out, size_t outsz)
{
    out[0] = '\0';
    if (!im || !p) return;
    if (p->orange) stage_append(out, outsz, "orange ");
    if (p->blue) stage_append(out, outsz, "blue ");
    if (p->fill) stage_append(out, outsz, "backing/fill ");
    if (p->strip) stage_append(out, outsz, "strip ");
    if (p->max_pixel < 16) stage_append(out, outsz, "low-color ");
    if (!out[0]) stage_append(out, outsz, "inspect");
}

static int repair_score(const Img *im, const ImgRepairProfile *p)
{
    int score = 0;
    if (!im || !p) return 0;
    if (p->orange) score += 40;
    if (p->blue) score += 22;
    if (p->fill) score += 30;
    if (p->strip) score += 18;
    if (p->area >= 2048) score += 12;
    if (p->coverage_pct >= 25 && p->coverage_pct <= 95) score += 10;
    if (p->max_pixel < 64) score += 6;
    if (g_hl_obj >= 0 && g_hl_obj < g_no) {
        Img *sel = img_find(g_obj[g_hl_obj].ii);
        if (sel) {
            int dw = abs(sel->w - im->w);
            int dh = abs(sel->h - im->h);
            if (dw <= sel->w / 3 && dh <= sel->h / 2) score += 24;
            if (im->w >= sel->w / 2 && im->h >= sel->h / 2) score += 12;
        }
    }
    return score;
}

void draw_mk2_auto_repair_suggestions(void)
{
    ImGui::Text("Auto Repair Suggestions");
    ImGui::TextDisabled("Heuristics for likely missing stock pieces. Use as a triage list, not gospel.");
    int sel_layer = (g_hl_obj >= 0 && g_hl_obj < g_no) ? ((g_obj[g_hl_obj].wx >> 8) & 0xFF) : 0x40;

    if (ImGui::BeginTable("repair_suggest", 8,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 210)))
    {
        ImGui::TableSetupColumn("ii", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("score", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("cov", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("reason", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("place");
        ImGui::TableSetupColumn("more");
        ImGui::TableHeadersRow();

        int top_i[6];
        int top_score[6];
        for (int t = 0; t < 6; t++) { top_i[t] = -1; top_score[t] = -1; }
        for (int i = 0; i < g_ni; i++) {
            if (image_use_count(g_img[i].idx) != 0) continue;
            ImgRepairProfile p = analyze_repair_profile(&g_img[i]);
            int score = repair_score(&g_img[i], &p);
            if (score < 35) continue;
            for (int t = 0; t < 6; t++) {
                if (score <= top_score[t]) continue;
                for (int s = 5; s > t; s--) {
                    top_i[s] = top_i[s - 1];
                    top_score[s] = top_score[s - 1];
                }
                top_i[t] = i;
                top_score[t] = score;
                break;
            }
        }

        for (int rank = 0; rank < 6; rank++) {
            int best_i = top_i[rank];
            int best_score = top_score[rank];
            if (best_i < 0) break;

            Img *im = &g_img[best_i];
            ImgRepairProfile p = analyze_repair_profile(im);
            char reason[128];
            repair_reason(im, &p, reason, sizeof reason);

            ImGui::PushID(best_i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", im->idx);
            ImGui::TableNextColumn(); ImGui::Text("%d", best_score);
            ImGui::TableNextColumn(); ImGui::Text("%dx%d", im->w, im->h);
            ImGui::TableNextColumn(); ImGui::Text("%d%%", p.coverage_pct);
            ImGui::TableNextColumn();
            ImVec4 avg(p.avg_r / 255.0f, p.avg_g / 255.0f, p.avg_b / 255.0f, 1.0f);
            ImGui::ColorButton("##avg", avg, ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("avg #%02X%02X%02X", p.avg_r, p.avg_g, p.avg_b);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(reason);
            ImGui::TableNextColumn();
            if (g_hl_obj >= 0 && g_hl_obj < g_no) {
                if (ImGui::SmallButton("Behind Sel")) {
                    int x = g_obj[g_hl_obj].depth;
                    int y = g_obj[g_hl_obj].sy;
                    int layer = mk2_layer_behind(sel_layer);
                    if (mk2_add_object_for_image(best_i, x, y, layer, im->pal_idx, 0, 0, true))
                        stage_set_toast("Placed suggested asset behind selected object");
                }
            } else {
                ImGui::TextDisabled("select obj");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Center")) {
                int x = 0, y = 0;
                mk2_find_center_fit_for_image(im, &x, &y);
                if (mk2_add_object_for_image(best_i, x, y, g_unused_enable_layer, im->pal_idx, 0, 0, true))
                    stage_set_toast("Placed suggested asset at module center");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("For Dead Pool orange vent backing, orange/fill/strip suggestions are the first places to inspect.");
}
