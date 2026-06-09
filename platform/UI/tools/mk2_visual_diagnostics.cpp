#include "bg_editor_globals.h"

#include "imgui.h"

#include <stdlib.h>
#include <string.h>

struct EdgeAvg {
    int count;
    int r;
    int g;
    int b;
};

static EdgeAvg mk2_edge_average(int obj_i, int x, int y1, int y2, int horizontal)
{
    EdgeAvg e;
    memset(&e, 0, sizeof e);
    if (obj_i < 0 || obj_i >= g_no) return e;
    Obj *o = &g_obj[obj_i];
    Img *im = img_find(o->ii);
    if (!im) return e;
    int pal_i = o->fl;
    if (pal_i < 0 || pal_i >= g_n_pals) return e;
    Uint64 sr = 0, sg = 0, sb = 0;
    if (horizontal) {
        int y = x;
        int xa = y1;
        int xb = y2;
        for (int wx = xa; wx < xb; wx++) {
            int px = object_pixel_at_world(o, im, wx, y);
            if (px <= 0) continue;
            Uint32 c = g_pals[pal_i][px & 0xFF];
            sr += (c >> 16) & 0xFF;
            sg += (c >> 8) & 0xFF;
            sb += c & 0xFF;
            e.count++;
        }
    } else {
        for (int wy = y1; wy < y2; wy++) {
            int px = object_pixel_at_world(o, im, x, wy);
            if (px <= 0) continue;
            Uint32 c = g_pals[pal_i][px & 0xFF];
            sr += (c >> 16) & 0xFF;
            sg += (c >> 8) & 0xFF;
            sb += c & 0xFF;
            e.count++;
        }
    }
    if (e.count > 0) {
        e.r = (int)(sr / (Uint64)e.count);
        e.g = (int)(sg / (Uint64)e.count);
        e.b = (int)(sb / (Uint64)e.count);
    }
    return e;
}

static int edge_delta(const EdgeAvg *a, const EdgeAvg *b)
{
    if (!a || !b || a->count == 0 || b->count == 0) return -1;
    return abs(a->r - b->r) + abs(a->g - b->g) + abs(a->b - b->b);
}

struct SeamCandidate {
    int a;
    int b;
    int delta;
    int x;
    int y;
    int len;
    int vertical;
    EdgeAvg ea;
    EdgeAvg eb;
};

static void add_seam_candidate(SeamCandidate *top, int top_n, const SeamCandidate *c)
{
    if (!top || !c || c->delta < g_seam_threshold) return;
    for (int i = 0; i < top_n; i++) {
        if (c->delta <= top[i].delta) continue;
        for (int j = top_n - 1; j > i; j--) top[j] = top[j - 1];
        top[i] = *c;
        return;
    }
}

void draw_mk2_palette_seam_detector(void)
{
    ImGui::Text("Palette Seam Detector");
    ImGui::TextDisabled("Finds adjacent object edges whose average colors jump sharply.");
    ImGui::SliderInt("Delta Threshold", &g_seam_threshold, 0, 255);
    ImGui::SliderInt("Max Edge Gap", &g_seam_max_gap, 0, 8);
    ImGui::Checkbox("Same layer only", &g_seam_same_layer_only);

    SeamCandidate top[8];
    memset(top, 0, sizeof top);
    for (int i = 0; i < 8; i++) { top[i].a = -1; top[i].b = -1; top[i].delta = -1; }

    for (int ai = 0; ai < g_no; ai++) {
        if (g_obj_hidden[ai]) continue;
        Obj *a = &g_obj[ai];
        Img *aim = img_find(a->ii);
        if (!aim) continue;
        int alayer = (a->wx >> 8) & 0xFF;
        int ax1 = a->depth, ax2 = a->depth + aim->w;
        int ay1 = a->sy, ay2 = a->sy + aim->h;

        for (int bi = ai + 1; bi < g_no; bi++) {
            if (g_obj_hidden[bi]) continue;
            Obj *b = &g_obj[bi];
            Img *bim = img_find(b->ii);
            if (!bim) continue;
            int blayer = (b->wx >> 8) & 0xFF;
            if (g_seam_same_layer_only && alayer != blayer) continue;
            int bx1 = b->depth, bx2 = b->depth + bim->w;
            int by1 = b->sy, by2 = b->sy + bim->h;

            int oy1 = ay1 > by1 ? ay1 : by1;
            int oy2 = ay2 < by2 ? ay2 : by2;
            if (oy2 > oy1) {
                if (abs(ax2 - bx1) <= g_seam_max_gap) {
                    EdgeAvg ea = mk2_edge_average(ai, ax2 - 1, oy1, oy2, 0);
                    EdgeAvg eb = mk2_edge_average(bi, bx1, oy1, oy2, 0);
                    SeamCandidate c = { ai, bi, edge_delta(&ea, &eb), bx1, oy1, oy2 - oy1, 1, ea, eb };
                    add_seam_candidate(top, 8, &c);
                }
                if (abs(bx2 - ax1) <= g_seam_max_gap) {
                    EdgeAvg ea = mk2_edge_average(ai, ax1, oy1, oy2, 0);
                    EdgeAvg eb = mk2_edge_average(bi, bx2 - 1, oy1, oy2, 0);
                    SeamCandidate c = { ai, bi, edge_delta(&ea, &eb), ax1, oy1, oy2 - oy1, 1, ea, eb };
                    add_seam_candidate(top, 8, &c);
                }
            }

            int ox1 = ax1 > bx1 ? ax1 : bx1;
            int ox2 = ax2 < bx2 ? ax2 : bx2;
            if (ox2 > ox1) {
                if (abs(ay2 - by1) <= g_seam_max_gap) {
                    EdgeAvg ea = mk2_edge_average(ai, ay2 - 1, ox1, ox2, 1);
                    EdgeAvg eb = mk2_edge_average(bi, by1, ox1, ox2, 1);
                    SeamCandidate c = { ai, bi, edge_delta(&ea, &eb), ox1, by1, ox2 - ox1, 0, ea, eb };
                    add_seam_candidate(top, 8, &c);
                }
                if (abs(by2 - ay1) <= g_seam_max_gap) {
                    EdgeAvg ea = mk2_edge_average(ai, ay1, ox1, ox2, 1);
                    EdgeAvg eb = mk2_edge_average(bi, by2 - 1, ox1, ox2, 1);
                    SeamCandidate c = { ai, bi, edge_delta(&ea, &eb), ox1, ay1, ox2 - ox1, 0, ea, eb };
                    add_seam_candidate(top, 8, &c);
                }
            }
        }
    }

    if (ImGui::BeginTable("seam_table", 8,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 180)))
    {
        ImGui::TableSetupColumn("delta", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("objs", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("dir", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("pos", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("len", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("action");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 8; i++) {
            if (top[i].a < 0) continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", top[i].delta);
            ImGui::TableNextColumn(); ImGui::Text("%d/%d", top[i].a, top[i].b);
            ImGui::TableNextColumn(); ImGui::Text("%s", top[i].vertical ? "V" : "H");
            ImGui::TableNextColumn(); ImGui::Text("%d,%d", top[i].x, top[i].y);
            ImGui::TableNextColumn(); ImGui::Text("%d", top[i].len);
            ImGui::TableNextColumn();
            ImGui::ColorButton("##a", ImVec4(top[i].ea.r/255.0f, top[i].ea.g/255.0f, top[i].ea.b/255.0f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(18,18));
            ImGui::TableNextColumn();
            ImGui::ColorButton("##b", ImVec4(top[i].eb.r/255.0f, top[i].eb.g/255.0f, top[i].eb.b/255.0f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(18,18));
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Select")) {
                editor_project_clear_selection();
                g_sel_flags[top[i].a] = 1;
                g_sel_flags[top[i].b] = 1;
                g_hl_obj = top[i].a;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ctr")) {
                g_view_x = top[i].x - 200;
                g_view_y = top[i].y - 120;
                g_view_changed = 1;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (top[0].a < 0)
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "No seams above threshold.");
}

static int mk2_visible_pixel_at(int wx, int wy, int min_layer, int max_layer)
{
    for (int i = g_no - 1; i >= 0; i--) {
        if (g_obj_hidden[i]) continue;
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        if (layer < min_layer || layer > max_layer) continue;
        Img *im = img_find(g_obj[i].ii);
        int px = object_pixel_at_world(&g_obj[i], im, wx, wy);
        if (px > 0) return 1;
    }
    return 0;
}

void draw_mk2_uppercut_headroom_preview(void)
{
    ImGui::Text("Uppercut Headroom Preview");
    ImGui::TextDisabled("Checks the world area exposed when MK2's camera lifts north during uppercuts.");
    ImGui::Checkbox("Use Stage Kit preview X", &g_headroom_use_preview_x);
    if (g_headroom_use_preview_x) g_headroom_world_x = g_stage_preview_worldx;
    ImGui::InputInt("World X", &g_headroom_world_x);
    ImGui::InputInt("Lift Pixels", &g_headroom_lift);
    ImGui::InputInt("Viewport Width", &g_headroom_width);
    if (g_headroom_lift < 0) g_headroom_lift = 0;
    if (g_headroom_width < 1) g_headroom_width = 1;

    int total = g_headroom_lift * g_headroom_width;
    int covered = 0;
    for (int y = -g_headroom_lift; y < 0; y++)
        for (int x = g_headroom_world_x; x < g_headroom_world_x + g_headroom_width; x++)
            covered += mk2_visible_pixel_at(x, y, 0x00, 0xFF);
    float pct = total > 0 ? (covered * 100.0f) / (float)total : 100.0f;
    if (pct >= 95.0f)
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "Headroom coverage: %.1f%%", pct);
    else if (pct >= 60.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1), "Headroom coverage: %.1f%%", pct);
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1), "Headroom coverage: %.1f%% - black top likely", pct);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ImGui::GetContentRegionAvail().x, 48.0f);
    if (sz.x < 80.0f) sz.x = 80.0f;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(20,20,28,255));
    for (int bx = 0; bx < (int)sz.x; bx++) {
        int wx = g_headroom_world_x + (int)((float)bx * (float)g_headroom_width / sz.x);
        int col_covered = 0;
        for (int y = -g_headroom_lift; y < 0; y++)
            col_covered += mk2_visible_pixel_at(wx, y, 0x00, 0xFF);
        float cp = g_headroom_lift > 0 ? (float)col_covered / (float)g_headroom_lift : 1.0f;
        ImU32 c = cp > 0.95f ? IM_COL32(90,210,120,220) : cp > 0.5f ? IM_COL32(230,180,70,220) : IM_COL32(220,60,60,220);
        dl->AddLine(ImVec2(p.x + bx, p.y + sz.y), ImVec2(p.x + bx, p.y + sz.y - cp * sz.y), c);
    }
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(180,180,210,180));
    ImGui::InvisibleButton("##headroom_chart", sz);

    if (ImGui::Button("Center Exposed Band", ImVec2(-1, 0))) {
        g_view_x = g_headroom_world_x;
        g_view_y = -g_headroom_lift - 12;
        g_view_changed = 1;
    }
}

void draw_mk2_parallax_sanity_checker(void)
{
    ImGui::Text("Parallax Sanity Checker");
    ImGui::TextDisabled("Flags layers whose scroll factor conflicts with their likely role.");
    int layer_vals[256], layer_n = 0, layer_counts[256], layer_pixels[256];
    memset(layer_counts, 0, sizeof layer_counts);
    memset(layer_pixels, 0, sizeof layer_pixels);
    for (int i = 0; i < g_no; i++) {
        Img *im = img_find(g_obj[i].ii);
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        int slot = -1;
        for (int j = 0; j < layer_n; j++) if (layer_vals[j] == layer) { slot = j; break; }
        if (slot < 0 && layer_n < 256) {
            slot = layer_n;
            layer_vals[layer_n++] = layer;
        }
        if (slot >= 0) {
            layer_counts[slot]++;
            if (im) layer_pixels[slot] += im->w * im->h;
        }
    }
    for (int a = 0; a < layer_n - 1; a++)
        for (int b = a + 1; b < layer_n; b++)
            if (layer_vals[a] > layer_vals[b]) {
                int tv = layer_vals[a]; layer_vals[a] = layer_vals[b]; layer_vals[b] = tv;
                int tc = layer_counts[a]; layer_counts[a] = layer_counts[b]; layer_counts[b] = tc;
                int tp = layer_pixels[a]; layer_pixels[a] = layer_pixels[b]; layer_pixels[b] = tp;
            }

    if (ImGui::BeginTable("parallax_sanity", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("factor", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("objs", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("role");
        ImGui::TableSetupColumn("status");
        ImGui::TableHeadersRow();
        for (int i = 0; i < layer_n; i++) {
            int layer = layer_vals[i];
            float f = mk2_scroll_factor_for_layer(layer);
            const char *role = "custom";
            const char *status = "ok";
            ImVec4 scol(0.45f, 1.0f, 0.55f, 1.0f);
            if (layer <= 0x32) role = "far background";
            else if (layer < 0x40) role = "mid/temple";
            else if (layer == 0x40 || layer == 0x41) role = "floor/play";
            else if (layer >= 0x43) role = "foreground";
            if (layer < 0x40 && f >= 0.95f) {
                status = "moves like floor";
                scol = ImVec4(1.0f, 0.35f, 0.25f, 1.0f);
            } else if ((layer == 0x40 || layer == 0x41) && f < 0.95f) {
                status = "floor too slow";
                scol = ImVec4(1.0f, 0.35f, 0.25f, 1.0f);
            } else if (layer >= 0x43 && f <= 1.0f) {
                status = "foreground not leading";
                scol = ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", layer);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", f);
            ImGui::TableNextColumn(); ImGui::Text("%d", layer_counts[i]);
            ImGui::TableNextColumn(); ImGui::Text("%s", role);
            ImGui::TableNextColumn(); ImGui::TextColored(scol, "%s", status);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Renderer factors: 0x32=.2, 0x3C=.5, 0x40/41=1.0, 0x43=1.2, 0x46=1.5.");
}

static int foreground_coverage_for_rect(int camera_x, int rx, int ry, int rw, int rh)
{
    int covered = 0;
    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            for (int i = g_no - 1; i >= 0; i--) {
                if (g_obj_hidden[i]) continue;
                int layer = (g_obj[i].wx >> 8) & 0xFF;
                if (layer < g_occ_min_layer) continue;
                Img *im = img_find(g_obj[i].ii);
                int px = object_pixel_at_screen(&g_obj[i], im, camera_x, x, y);
                if (px > 0) { covered++; break; }
            }
        }
    }
    return covered;
}

static void draw_occ_bar(const char *label, int covered, int total)
{
    float pct = total > 0 ? (covered * 100.0f) / (float)total : 0.0f;
    ImGui::Text("%s occlusion: %.1f%%", label, pct);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ImGui::GetContentRegionAvail().x, 14.0f);
    if (sz.x < 80.0f) sz.x = 80.0f;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(32,32,40,255));
    ImU32 c = pct > 45.0f ? IM_COL32(220,70,60,230) : pct > 20.0f ? IM_COL32(230,180,70,230) : IM_COL32(90,210,120,230);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x * pct / 100.0f, p.y + sz.y), c);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(160,160,180,180));
    ImGui::InvisibleButton(label, sz);
}

void draw_mk2_foreground_occlusion_preview(void)
{
    ImGui::Text("Foreground Occlusion Preview");
    ImGui::TextDisabled("Approximates player silhouettes against foreground layers.");
    ImGui::Checkbox("Use Stage Kit preview X", &g_occ_use_preview_x);
    if (g_occ_use_preview_x) g_occ_camera_x = g_stage_preview_worldx;
    ImGui::InputInt("Camera X", &g_occ_camera_x);
    ImGui::InputInt("Min Foreground Layer", &g_occ_min_layer);
    ImGui::InputInt("Player Y", &g_occ_player_y);
    ImGui::InputInt("Player W", &g_occ_player_w);
    ImGui::InputInt("Player H", &g_occ_player_h);
    ImGui::InputInt("P1 Screen X", &g_occ_p1_x);
    ImGui::InputInt("P2 Screen X", &g_occ_p2_x);
    if (g_occ_player_w < 1) g_occ_player_w = 1;
    if (g_occ_player_h < 1) g_occ_player_h = 1;
    int total = g_occ_player_w * g_occ_player_h;
    int p1 = foreground_coverage_for_rect(g_occ_camera_x, g_occ_p1_x, g_occ_player_y, g_occ_player_w, g_occ_player_h);
    int p2 = foreground_coverage_for_rect(g_occ_camera_x, g_occ_p2_x, g_occ_player_y, g_occ_player_w, g_occ_player_h);
    draw_occ_bar("P1", p1, total);
    draw_occ_bar("P2", p2, total);
    ImGui::TextDisabled("High values mean side towers/foreground may hide too much of a fighter.");
}
