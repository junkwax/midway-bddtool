#include "bg_editor_globals.h"
#include "imgui.h"

#include <stdio.h>
#include <string.h>

void draw_verify(void)
{
    if (!g_show_verify) return;
    ImGui::OpenPopup("File Verification");
    if (ImGui::BeginPopupModal("File Verification", &g_show_verify,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        int issues = 0;
        ImGui::Text("BDB: %s", g_bdb_path[0] ? g_bdb_path : "(none)");
        ImGui::Text("BDD: %s", g_bdd_path[0] ? g_bdd_path : "(none)");
        ImGui::Separator();
        if (g_name[0] && strlen(g_name) > 8) {
            ImGui::TextColored(ImVec4(1,0.6f,0,1),
                              "World name '%s' is longer than DOS 8.3 base name limit.", g_name);
            issues++;
        }

        int used_imgs = 0;
        for (int i = 0; i < g_ni; i++) {
            int used = 0;
            for (int oi = 0; oi < g_no; oi++)
                if (g_obj[oi].ii == g_img[i].idx) { used = 1; break; }
            if (used) used_imgs++;
            int pal_ok = (g_img[i].pal_idx >= 0 && g_img[i].pal_idx < g_n_pals);
            if (!pal_ok) {
                if (g_simple_mode)
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Image #%d: invalid palette %d",
                                      g_img[i].idx, g_img[i].pal_idx);
                else
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Image 0x%02X: invalid palette %d",
                                      g_img[i].idx, g_img[i].pal_idx);
                issues++;
            }
        }
        if (g_ni == 0) { ImGui::TextColored(ImVec4(1,0.6f,0,1), "No images loaded!"); issues++; }

        int unassigned = 0;
        for (int oi = 0; oi < g_no; oi++) {
            int found = 0;
            for (int ii = 0; ii < g_ni; ii++)
                if (g_img[ii].idx == g_obj[oi].ii) { found = 1; break; }
            if (!found) {
                if (g_simple_mode)
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Sprite %d: image missing from stage!",
                                      oi);
                else
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Object %d: ii=0x%04X not found in BDD!",
                                      oi, g_obj[oi].ii);
                issues++;
            }
            if (g_obj[oi].fl < 0 || g_obj[oi].fl >= g_n_pals) {
                ImGui::TextColored(ImVec4(1,0.5f,0.3f,1), "Object %d: palette %d out of range",
                                  oi, g_obj[oi].fl);
                issues++;
            }
            Img *obj_im = img_find(g_obj[oi].ii);
            int obj_w = obj_im ? obj_im->w : 1;
            int obj_h = obj_im ? obj_im->h : 1;
            if (g_bdb_num_modules > 0 && assign_module(g_obj[oi].depth, g_obj[oi].sy, obj_w, obj_h) < 0)
                unassigned++;
        }
        if (unassigned > 0) {
            if (g_simple_mode)
                ImGui::TextColored(ImVec4(1,0.6f,0,1),
                                  "%d sprite(s) are outside all stage regions and may not appear in-game.",
                                  unassigned);
            else
                ImGui::TextColored(ImVec4(1,0.6f,0,1),
                                  "%d object(s) are outside all module rectangles and will not export in BLKS.",
                                  unassigned);
            issues++;
        }

        if (g_have_bdb && g_bdb_header[0]) {
            int hdr_w, hdr_h, hdr_md, hdr_nm, hdr_np, hdr_no;
            char nm[64];
            if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
                       nm, &hdr_w, &hdr_h, &hdr_md, &hdr_nm, &hdr_np, &hdr_no) >= 7) {
                if (hdr_nm != g_bdb_num_modules) {
                    ImGui::TextColored(ImVec4(1,0.6f,0,1), "Module count mismatch: header=%d, actual=%d",
                                      hdr_nm, g_bdb_num_modules);
                    issues++;
                }
                if (hdr_no != g_no) {
                    ImGui::TextColored(ImVec4(1,0.6f,0,1), "Object count mismatch: header=%d, actual=%d",
                                      hdr_no, g_no);
                    issues++;
                }
                if (hdr_np != g_n_pals) {
                    ImGui::TextColored(ImVec4(1,0.6f,0,1), "Palette count mismatch: header=%d, actual=%d",
                                      hdr_np, g_n_pals);
                    issues++;
                }
            }
        }
        if (g_have_bdb && g_bdb_num_modules == 0 && g_no > 0) {
            ImGui::TextColored(ImVec4(1,0.6f,0,1),
                              "No modules: MK2 export will create a default full-world module.");
        }

        ImGui::Separator();
        if (issues == 0)
            ImGui::TextColored(ImVec4(0,1,0.5f,1), "No issues found. File looks valid!");
        else
            ImGui::TextColored(ImVec4(1,0.6f,0,1), "%d issue(s) found.", issues);
        ImGui::Text("%d images (%d used), %d objects, %d palettes, %d modules",
                    g_ni, used_imgs, g_no, g_n_pals, g_bdb_num_modules);
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(80, 0))) g_show_verify = false;
        ImGui::EndPopup();
    }
}
