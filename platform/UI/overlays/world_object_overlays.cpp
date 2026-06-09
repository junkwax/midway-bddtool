#include "bg_editor.h"
#include "bg_editor_globals.h"
#include <imgui.h>
#include <cstdio>

void draw_world_object_overlays(void)
{
        /* object labels: shown only when the object info overlay is enabled */
        if (g_show_labels && g_have_bdb && g_no > 0 && !g_preview_mode) {
            ImDrawList *dl = ImGui::GetForegroundDrawList();
            ImVec2 display_size = ImGui::GetIO().DisplaySize;
            for (int i = 0; i < g_no; i++) {
                if (g_obj_hidden[i]) continue;
                Obj *o = &g_obj[i];
                Img *im = img_find(o->ii);
                if (!im) continue;
                BddScreenRect screen_rect;
                if (!bdd_object_screen_rect(i, im->w, im->h, g_view_x, g_view_y,
                                            g_zoom, (int)display_size.x,
                                            (int)display_size.y, g_scroll_pos,
                                            &screen_rect))
                    continue;
                char lbl[128];
                if (g_zoom >= 2) {
                    snprintf(lbl, sizeof lbl,
                             "obj[%d]\nx=%d y=%d\nii=0x%04X pal=%d\nwx=0x%04X",
                             i, o->depth, o->sy, o->ii, o->fl, o->wx);
                } else {
                    snprintf(lbl, sizeof lbl, "%d (%d,%d)", i, o->depth, o->sy);
                }
                ImU32 col = g_sel_flags[i] ? IM_COL32(255,220,80,230) : IM_COL32(200,200,220,160);
                ImVec2 pos((float)screen_rect.x + 2, (float)screen_rect.y + 2);
                /* background rect for readability */
                ImVec2 ts = ImGui::CalcTextSize(lbl);
                dl->AddRectFilled(ImVec2(pos.x-1, pos.y-1), ImVec2(pos.x+ts.x+1, pos.y+ts.y+1),
                                  IM_COL32(10,10,18,180));
                dl->AddText(pos, col, lbl);
            }
        }
    
        /* layer tint overlay */
        if (g_layer_tint && g_have_bdb && g_no > 0 && !g_preview_mode) {
            static const struct { int wx; ImU32 col; } lc[] = {
                {0x32, IM_COL32( 60,120,220, 60)}, /* Sky/back */
                {0x3C, IM_COL32( 80,200,120, 60)}, /* Mid */
                {0x40, IM_COL32(220,180, 50, 55)}, /* Floor/play */
                {0x41, IM_COL32(220,160, 50, 55)}, /* Floor alt */
                {0x43, IM_COL32(200, 80, 80, 60)}, /* Near foreground */
                {0x46, IM_COL32(180, 80,220, 60)}, /* Front foreground */
            };
            ImDrawList *fdl = ImGui::GetForegroundDrawList();
            ImVec2 display_size = ImGui::GetIO().DisplaySize;
            for (int i = 0; i < g_no; i++) {
                if (g_obj_hidden[i]) continue;
                Obj *o = &g_obj[i];
                Img *im = img_find(o->ii);
                if (!im) continue;
                int layer = (o->wx >> 8) & 0xFF;
                ImU32 tc = IM_COL32(128,128,128,40);
                for (int li = 0; li < 6; li++) if (lc[li].wx == layer) { tc = lc[li].col; break; }
                BddScreenRect screen_rect;
                if (!bdd_object_screen_rect(i, im->w, im->h, g_view_x, g_view_y,
                                            g_zoom, (int)display_size.x,
                                            (int)display_size.y, g_scroll_pos,
                                            &screen_rect))
                    continue;
                fdl->AddRectFilled(ImVec2((float)screen_rect.x, (float)screen_rect.y),
                                   ImVec2((float)(screen_rect.x + screen_rect.w),
                                          (float)(screen_rect.y + screen_rect.h)), tc);
            }
        }
    
    
}
