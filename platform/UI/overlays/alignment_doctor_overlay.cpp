#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static bool alignment_object_screen_rect(int obj_i, int *sx, int *sy,
                                         int *sw, int *sh, float *scroll_factor,
                                         int *world_x, int *world_y)
{
    if (obj_i < 0 || obj_i >= g_no) return false;
    Obj *o = &g_obj[obj_i];
    Img *im = img_find(o->ii);
    if (!im || im->w <= 0 || im->h <= 0) return false;

    int ox = o->depth;
    int oy = o->sy;
    float sf = bdd_object_game_scroll_factor(obj_i);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    BddScreenRect rect;
    if (!bdd_object_screen_rect(obj_i, im->w, im->h,
                                g_view_x, g_view_y, g_zoom,
                                (int)ds.x, (int)ds.y, g_scroll_pos,
                                &rect))
        return false;
    if (g_game_view) {
        bdd_object_game_origin(obj_i, &ox, &oy);
    } else {
        bdd_object_editor_origin(obj_i, &ox, &oy);
    }
    if (sx) *sx = rect.x;
    if (sy) *sy = rect.y;
    if (sw) *sw = rect.w;
    if (sh) *sh = rect.h;
    if (scroll_factor) *scroll_factor = sf;
    if (world_x) *world_x = ox;
    if (world_y) *world_y = oy;
    return true;
}

static void draw_alignment_doctor_label(ImDrawList *dl, ImVec2 pos,
                                        const char *text, ImU32 text_col)
{
    ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(ImVec2(pos.x - 3.0f, pos.y - 2.0f),
                      ImVec2(pos.x + ts.x + 3.0f, pos.y + ts.y + 2.0f),
                      IM_COL32(5, 8, 14, 205), 3.0f);
    dl->AddText(pos, text_col, text);
}

void draw_alignment_doctor_overlay(void)
{
    if (!g_show_alignment_doctor || !g_have_bdb || g_no <= 0 || g_preview_mode)
        return;

    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return;
    std::vector<unsigned char> target((size_t)object_cap, 0);
    int target_count = 0;
    for (int i = 0; i < g_no; i++) {
        if (g_sel_flags[i]) {
            target[i] = 1;
            target_count++;
        }
    }
    if (target_count == 0 && g_hl_obj >= 0 && g_hl_obj < g_no) {
        target[g_hl_obj] = 1;
        target_count++;
    }
    if (g_hover_obj >= 0 && g_hover_obj < g_no && !target[g_hover_obj]) {
        target[g_hover_obj] = 1;
        target_count++;
    }
    if (target_count == 0) return;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImU32 full_col = IM_COL32(100, 205, 255, 230);
    ImU32 visible_col = IM_COL32(95, 255, 135, 235);
    ImU32 module_col = IM_COL32(130, 150, 255, 115);
    ImU32 pivot_col = IM_COL32(255, 120, 230, 240);
    ImU32 label_col = IM_COL32(230, 246, 255, 245);

    for (int i = 0; i < g_no; i++) {
        if (!target[i] || g_obj_hidden[i]) continue;
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im) continue;

        int sx = 0, sy = 0, sw = 0, sh = 0;
        int ox = 0, oy = 0;
        float sf = 1.0f;
        if (!alignment_object_screen_rect(i, &sx, &sy, &sw, &sh, &sf, &ox, &oy))
            continue;

        ImVec2 p0((float)sx, (float)sy);
        ImVec2 p1((float)(sx + sw), (float)(sy + sh));
        dl->AddRect(p0, p1, full_col, 0.0f, 0, 2.0f);

        int vx1 = 0, vy1 = 0, vx2 = 0, vy2 = 0;
        bool has_visible = image_nonzero_bounds(im, &vx1, &vy1, &vx2, &vy2) != 0;
        if (has_visible) {
            int lx1 = o->hfl ? (im->w - 1 - vx2) : vx1;
            int lx2 = o->hfl ? (im->w - 1 - vx1) : vx2;
            int ly1 = o->vfl ? (im->h - 1 - vy2) : vy1;
            int ly2 = o->vfl ? (im->h - 1 - vy1) : vy2;
            ImVec2 vp0((float)(sx + lx1 * g_zoom), (float)(sy + ly1 * g_zoom));
            ImVec2 vp1((float)(sx + (lx2 + 1) * g_zoom), (float)(sy + (ly2 + 1) * g_zoom));
            dl->AddRect(vp0, vp1, visible_col, 0.0f, 0, 2.0f);
        }

        bool has_pivot = (im->anix || im->aniy || im->anix2 || im->aniy2 || im->aniz2);
        int px = img_anim_offset_x(im, o->hfl);
        int py = img_anim_offset_y(im, o->vfl);
        if (has_pivot && px >= 0 && py >= 0 && px < im->w && py < im->h) {
            float psx = (float)(sx + px * g_zoom) + (float)g_zoom * 0.5f;
            float psy = (float)(sy + py * g_zoom) + (float)g_zoom * 0.5f;
            dl->AddLine(ImVec2(psx - 7.0f, psy), ImVec2(psx + 7.0f, psy), pivot_col, 2.0f);
            dl->AddLine(ImVec2(psx, psy - 7.0f), ImVec2(psx, psy + 7.0f), pivot_col, 2.0f);
            dl->AddCircle(ImVec2(psx, psy), 4.0f, pivot_col, 0, 1.5f);
        }

        char mod_name[64] = "none";
        int mx1 = 0, mx2 = 0, my1 = 0, my2 = 0;
        int mod = assign_module(o->depth, o->sy, im->w, im->h);
        int local_x = 0, local_y = 0;
        if (mod >= 0 && parse_module_bounds(mod, mod_name, &mx1, &mx2, &my1, &my2)) {
            local_x = o->depth - mx1;
            local_y = o->sy - my1;
            if (!g_game_view && !g_runtime_layout_view) {
                ImVec2 ds = ImGui::GetIO().DisplaySize;
                BddScreenRect mrect;
                if (bdd_world_rect_screen_rect(mx1, my1, mx2 + 1, my2 + 1,
                                               g_view_x, g_view_y, g_zoom,
                                               (int)ds.x, (int)ds.y, &mrect)) {
                    ImVec2 mp0((float)mrect.x, (float)mrect.y);
                    ImVec2 mp1((float)(mrect.x + mrect.w),
                               (float)(mrect.y + mrect.h));
                    dl->AddRect(mp0, mp1, module_col, 0.0f, 0, 1.0f);
                }
            }
        }

        int layer = (o->wx >> 8) & 0xFF;
        char visible_buf[40] = "vis=empty";
        if (has_visible)
            snprintf(visible_buf, sizeof visible_buf, "vis=(%d,%d) %dx%d",
                     vx1, vy1, vx2 - vx1 + 1, vy2 - vy1 + 1);
        char pivot_buf[40] = "pivot=none";
        if (has_pivot)
            snprintf(pivot_buf, sizeof pivot_buf, "pivot=(%d,%d)", px, py);

        char lbl[320];
        snprintf(lbl, sizeof lbl,
                 "obj[%d] ii=0x%04X pal=%d\n"
                 "full=(%d,%d) %dx%d  %s\n"
                 "module=%s local=(%d,%d)\n"
                 "layer=0x%02X %s %.2fx  %s",
                 i, o->ii, o->fl,
                 o->depth, o->sy, im->w, im->h, visible_buf,
                 mod >= 0 ? mod_name : "none", local_x, local_y,
                 layer, mk2_layer_label(layer), sf, pivot_buf);
        draw_alignment_doctor_label(dl, ImVec2((float)sx + 4.0f, (float)(sy + sh) + 4.0f),
                                    lbl, label_col);
    }
}
