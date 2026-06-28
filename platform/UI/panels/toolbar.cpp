#include "UI/panels/ToolboxPanel.h"
#include "UI/panels/ToolbarPanel.h"
#include "bg_editor_globals.h"
#include "Core/editor_commands.h"
#include "Core/world_module_utils.h"
#include "UI/actions/object_position_undo.h"
#include "undo_manager.h"

#include "imgui.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>
/* ---- toolbar ------------------------------------------------------ */

/* Toolbar button helper: styled as active (highlighted) when `active` is true */
static bool tb_button(const char *label, bool active, const char *tooltip = nullptr,
                      bool disabled = false)
{
    if (disabled) ImGui::BeginDisabled();
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f,0.45f,0.80f,1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f,0.52f,0.90f,1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f,0.58f,1.00f,1.00f));
    }
    bool clicked = ImGui::Button(label);
    if (active) ImGui::PopStyleColor(3);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    if (disabled) ImGui::EndDisabled();
    return clicked;
}

static void tb_sep(void)
{
    ImGui::SameLine(0, 6);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f,0.35f,0.35f,1.0f));
    ImGui::Text("|");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 6);
}

void ToolbarPanel::render()
{
    /* full-width toolbar strip — sits as a fixed window just below the menu bar */
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float menu_h = ImGui::GetFrameHeight();  /* menu bar bottom — toolbar docks flush below it */
    ImGui::SetNextWindowPos(ImVec2(0, menu_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ds.x, 38), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.20f, 0.20f, 0.25f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(10, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));

    bool open = ImGui::Begin("##toolbar", NULL,
        ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (!open) {
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        return;
    }

    /* vertically center the single button row within the 38px strip */
    {
        float row_h = ImGui::GetFrameHeight();
        float cy = (38.0f - row_h) * 0.5f;
        if (cy > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(cy);
    }

    /* ── File ─────────────────────────────────────── */
    if (tb_button("New",  false, "New stage  (Ctrl+N)"))
        editor_emit_unsaved_action(UNSAVED_ACTION_SHOW_NEW_PROJECT);
    ImGui::SameLine(0, 3);
    if (tb_button("Open", false, "Open stage  (Ctrl+O)")) {
        char p[512] = "";
        if (file_dialog_open("Open Stage",
            "Stage Files\0*.BDB;*.bdb;*.BDD;*.bdd\0All Files\0*.*\0", p, sizeof p))
        {
            editor_emit_unsaved_action(UNSAVED_ACTION_OPEN_STAGE, p);
        }
    }
    ImGui::SameLine(0, 3);
    {
        bool can_save = (g_have_bdb || g_no > 0 || g_ni > 0) && (g_bdb_path[0] || g_bdd_path[0]);
        if (tb_button("Save", false, "Save BDB + BDD  (Ctrl+S)", !can_save)) {
            editor_emit_save_all();
            g_hint_save = false;
        }
        hint_badge(&g_hint_save, "hint_save");
    }

    tb_sep();

    /* ── Edit ─────────────────────────────────────── */
    if (tb_button("<  Undo", false, "Undo  (Ctrl+Z)", !undo_is_available()))
        undo_restore();
    ImGui::SameLine(0, 3);
    if (tb_button("Redo  >", false, "Redo  (Ctrl+Y)", !redo_is_available()))
        redo_restore();

    tb_sep();

    /* ── Tools ────────────────────────────────────── */
    int toolbar_sel_count = selected_count();
    const char *select_label = (g_cur_tool == 0 && toolbar_sel_count > 0) ? "Unselect" : "Select";
    const char *select_tip = (g_cur_tool == 0 && toolbar_sel_count > 0)
        ? "Clear selection and switch to Pan"
        : "Select / move objects; double-click the canvas to switch to Pan  (S)";
    const char *tool_labels[] = { select_label,"Place","Pan","Zoom","Brush" };
    const char *tool_tips[]   = {
        select_tip,
        "Place image once, then select it  (P)",
        "Pan viewport; click an object to switch back to Select  (H)",
        "Zoom in/out  (Z)",
        "Pixel brush  (B)"
    };
    for (int t = 0; t < 5; t++) {
        if (t > 0) ImGui::SameLine(0, 2);
        if (tb_button(tool_labels[t], g_cur_tool == t, tool_tips[t])) {
            if (t == 0 && g_cur_tool == 0 && toolbar_sel_count > 0) {
                editor_project_clear_selection();
                g_hl_obj = -1;
                g_cur_tool = 2;
                snprintf(g_toast_msg, sizeof g_toast_msg, "Selection cleared; Pan tool active");
                g_toast_timer = 2.0f;
            } else {
                g_cur_tool = t;
                if (t == 1) g_hint_place = false;
            }
        }
    }
    if (g_cur_tool == 1) hint_badge(&g_hint_place, "hint_place");

    if (toolbar_sel_count > 0 || g_clip_count > 0) {
        tb_sep();
        char copy_tip[96];
        snprintf(copy_tip, sizeof copy_tip, "Copy selected group  (Ctrl+C)  selected:%d", toolbar_sel_count);
        if (tb_button("Copy", false, copy_tip, toolbar_sel_count == 0)) {
            int copied = copy_selected_objects_to_clipboard();
            snprintf(g_toast_msg, sizeof g_toast_msg, "Copied %d object(s)", copied);
            g_toast_timer = 3.0f;
        }
        ImGui::SameLine(0, 3);
        if (tb_button("Paste", false, "Paste copied group below nearest asset  (Ctrl+V)", g_clip_count == 0))
            paste_clipboard_objects(32, 16);
    }

    tb_sep();

    /* ── Import ───────────────────────────────────── */
    if (tb_button("Import PNG", false, "Import PNG as image  (Ctrl+L)")) {
        char path[512] = "";
        if (file_dialog_open("Import PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0",
                             path, sizeof path))
            import_png(path);
    }
    hint_badge(&g_hint_import, "hint_import");

    tb_sep();

    /* ── View ─────────────────────────────────────── */
    if (tb_button(g_game_view ? "Exit Preview" : "Game Preview",
                  g_game_view, "Toggle game preview mode")) {
        bool entering_preview = g_game_view == 0;
        g_game_view ^= 1;
        if (entering_preview) {
            g_runtime_layout_view = 1;
            route_to_game_preview_screen(true, true);
            g_gv_needs_autozoom = true;
        } else {
            g_runtime_layout_view = 0;
            focus_editor_on_game_preview_screen();
        }
    }
    ImGui::SameLine(0, 3);
    if (tb_button("Runtime", g_runtime_layout_view != 0,
                  "Show the MK2 runtime-local layout in Game Preview",
                  !g_have_bdb || g_no == 0)) {
        bool turning_on = g_runtime_layout_view == 0;
        g_runtime_layout_view ^= 1;
        g_split_view = 0;
        if (turning_on && !g_game_view)
            g_game_view = 1;
        route_to_game_preview_screen(true, g_game_view != 0);
        if (g_game_view)
            g_gv_needs_autozoom = true;
    }
    ImGui::SameLine(0, 3);
    if (tb_button("Fit All", false, "Fit entire stage in world view - shows all objects including floor", !g_have_bdb || g_no == 0)) {
        g_game_view = 0;
        g_runtime_layout_view = 0;
        zoom_to_fit();
    }
    ImGui::SameLine(0, 3);
    {
        bool has_stage = g_have_bdb && g_bdb_path[0] && g_bdd_path[0];
        if (tb_button("Overview", stage_overview_is_open(), "Full stage composite (all layers)", !has_stage))
            toggle_stage_overview();
    }
    ImGui::SameLine(0, 3);
    if (tb_button("Borders", (bool)g_show_borders, "Show/hide object borders in world view  (Shift+B)", g_game_view != 0))
        g_show_borders ^= 1;
    ImGui::SameLine(0, 3);
    if (tb_button("Labels", (bool)g_show_labels, "Show object info overlay  (O)"))
        g_show_labels ^= 1;
    ImGui::SameLine(0, 3);
    if (tb_button("Layers",    g_show_layers,  "Layers panel"))
        g_show_layers ^= 1;
    ImGui::SameLine(0, 3);
    bool modules_visible = g_show_modules || g_show_module_bounds;
    if (tb_button(g_simple_mode ? "Regions" : "Modules",
                  modules_visible,
                  g_simple_mode ? "Show/hide stage regions and bounds" : "Show/hide LOAD2 modules panel and bounds",
                  !g_have_bdb)) {
        if (modules_visible) {
            g_show_modules = false;
            g_show_module_bounds = false;
        } else {
            g_show_modules = true;
            g_show_module_bounds = true;
        }
    }
    ImGui::SameLine(0, 3);
    if (tb_button("Help",      g_show_help,    "Keyboard shortcuts  (F1)"))
        g_show_help ^= 1;

    /* ── Zoom readout (right side) ────────────────── */
    {
        float avail = ImGui::GetContentRegionAvail().x;
        char zbuf[32];
        int sel_c = 0; for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_c++;
        int mod_sel_c = module_selection_count();
        /* alignment/distribute buttons need objects; center can also target highlighted modules. */
        if ((sel_c >= 2 || mod_sel_c > 0) && g_have_bdb && avail > 300) {
            tb_sep();
            if (sel_c >= 2 && sel_c != g_no && mod_sel_c == 0) {
            if (tb_button("Align L", false, "Align left edges")) {
                ObjectPositionUndoCapture undo;
                if (object_position_undo_capture_selected(&undo)) {
                    int r = INT_MAX;
                    for (int i = 0; i < g_no; i++) if (g_sel_flags[i] && g_obj[i].depth < r) r = g_obj[i].depth;
                    for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) g_obj[i].depth = r;
                    if (object_position_undo_commit(&undo, "Align Left") > 0) g_need_rebuild = 1;
                }
            } ImGui::SameLine(0,2);
            if (tb_button("Align R", false, "Align right edges")) {
                ObjectPositionUndoCapture undo;
                if (object_position_undo_capture_selected(&undo)) {
                    int r = -1;
                    for (int i = 0; i < g_no; i++) {
                        if (!g_sel_flags[i]) continue;
                        Img *im = img_find(g_obj[i].ii);
                        int b = g_obj[i].depth + (im ? im->w : 0);
                        if (b > r) r = b;
                    }
                    for (int i = 0; i < g_no; i++) {
                        if (!g_sel_flags[i]) continue;
                        Img *im = img_find(g_obj[i].ii);
                        g_obj[i].depth = r - (im ? im->w : 0);
                    }
                    if (object_position_undo_commit(&undo, "Align Right") > 0) g_need_rebuild = 1;
                }
            } ImGui::SameLine(0,2);
            if (tb_button("Align T", false, "Align top edges")) {
                ObjectPositionUndoCapture undo;
                if (object_position_undo_capture_selected(&undo)) {
                    int r = INT_MAX;
                    for (int i = 0; i < g_no; i++) if (g_sel_flags[i] && g_obj[i].sy < r) r = g_obj[i].sy;
                    for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) g_obj[i].sy = r;
                    if (object_position_undo_commit(&undo, "Align Top") > 0) g_need_rebuild = 1;
                }
            } ImGui::SameLine(0,2);
            if (tb_button("Align B", false, "Align bottom edges")) {
                ObjectPositionUndoCapture undo;
                if (object_position_undo_capture_selected(&undo)) {
                    int r = -1;
                    for (int i = 0; i < g_no; i++) {
                        if (!g_sel_flags[i]) continue;
                        Img *im = img_find(g_obj[i].ii);
                        int b = g_obj[i].sy + (im ? im->h : 0);
                        if (b > r) r = b;
                    }
                    for (int i = 0; i < g_no; i++) {
                        if (!g_sel_flags[i]) continue;
                        Img *im = img_find(g_obj[i].ii);
                        g_obj[i].sy = r - (im ? im->h : 0);
                    }
                    if (object_position_undo_commit(&undo, "Align Bottom") > 0) g_need_rebuild = 1;
                }
            }
            /* distribute — needs ≥3 */
            if (sel_c >= 3) {
                ImGui::SameLine(0, 6);
                /* distribute H */
                if (tb_button("Dist H", false, "Distribute horizontally (equal gaps)")) {
                    ObjectPositionUndoCapture undo;
                    if (object_position_undo_capture_selected(&undo)) {
                        std::vector<int> s_ti;
                        s_ti.reserve((size_t)g_no);
                        for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) s_ti.push_back(i);
                        int tn = (int)s_ti.size();
                        for (int a=1;a<tn;a++){int t=s_ti[a],j=a-1;while(j>=0&&g_obj[s_ti[j]].depth>g_obj[t].depth){s_ti[j+1]=s_ti[j];j--;}s_ti[j+1]=t;}
                        Img*iml=img_find(g_obj[s_ti[tn-1]].ii);
                        int x0=g_obj[s_ti[0]].depth, x1=g_obj[s_ti[tn-1]].depth+(iml?iml->w:0), tw=0;
                        for(int k=0;k<tn;k++){Img*im=img_find(g_obj[s_ti[k]].ii);tw+=(im?im->w:0);}
                        int gap=(tn>1)?(x1-x0-tw)/(tn-1):0;
                        int cx=x0; for(int k=0;k<tn;k++){Img*im=img_find(g_obj[s_ti[k]].ii);g_obj[s_ti[k]].depth=cx;cx+=(im?im->w:0)+gap;}
                        if (object_position_undo_commit(&undo, "Distribute H") > 0) g_need_rebuild=1;
                    }
                } ImGui::SameLine(0,2);
                /* distribute V */
                if (tb_button("Dist V", false, "Distribute vertically (equal gaps)")) {
                    ObjectPositionUndoCapture undo;
                    if (object_position_undo_capture_selected(&undo)) {
                        std::vector<int> s_ti2;
                        s_ti2.reserve((size_t)g_no);
                        for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) s_ti2.push_back(i);
                        int tn = (int)s_ti2.size();
                        for (int a=1;a<tn;a++){int t=s_ti2[a],j=a-1;while(j>=0&&g_obj[s_ti2[j]].sy>g_obj[t].sy){s_ti2[j+1]=s_ti2[j];j--;}s_ti2[j+1]=t;}
                        Img*iml=img_find(g_obj[s_ti2[tn-1]].ii);
                        int y0=g_obj[s_ti2[0]].sy, y1=g_obj[s_ti2[tn-1]].sy+(iml?iml->h:0), th=0;
                        for(int k=0;k<tn;k++){Img*im=img_find(g_obj[s_ti2[k]].ii);th+=(im?im->h:0);}
                        int gap=(tn>1)?(y1-y0-th)/(tn-1):0;
                        int cy=y0; for(int k=0;k<tn;k++){Img*im=img_find(g_obj[s_ti2[k]].ii);g_obj[s_ti2[k]].sy=cy;cy+=(im?im->h:0)+gap;}
                        if (object_position_undo_commit(&undo, "Distribute V") > 0) g_need_rebuild=1;
                    }
                }
            }
            }
            /* center on stage — parse stage dimensions */
            {
                int stg_w2 = 1024, stg_h2 = 256;
                if (g_bdb_header[0]) {
                    char _n2[64]; int _d2,_m2,_p2,_o2;
                    sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
                           _n2, &stg_w2, &stg_h2, &_d2, &_m2, &_p2, &_o2);
                }
                auto do_center_toolbar = [&](bool horiz) {
                    if (module_selection_count() == 0 && sel_c == g_no && g_no > 0) {
                        module_selection_set_all(true);
                        mod_sel_c = module_selection_count();
                    }
                    int sl=INT_MAX, sr=INT_MIN, st=INT_MAX, sb=INT_MIN;
                    bool any = false;
                    for(int i=0;i<g_no;i++){
                        if(!g_sel_flags[i])continue;
                        Img*im=img_find(g_obj[i].ii);
                        if(g_obj[i].depth<sl)sl=g_obj[i].depth;
                        int r=g_obj[i].depth+(im?im->w:0); if(r>sr)sr=r;
                        if(g_obj[i].sy<st)st=g_obj[i].sy;
                        int b=g_obj[i].sy+(im?im->h:0); if(b>sb)sb=b;
                        any = true;
                    }
                    int mx1=0,mx2=0,my1=0,my2=0;
                    if(module_selection_bounds(&mx1,&mx2,&my1,&my2)){
                        if(mx1<sl)sl=mx1; if(mx2+1>sr)sr=mx2+1;
                        if(my1<st)st=my1; if(my2+1>sb)sb=my2+1;
                        any = true;
                    }
                    if(!any)return;
                    int dx=horiz?((stg_w2-(sr-sl))/2-sl):0;
                    int dy=horiz?0:((stg_h2-(sb-st))/2-st);
                    if(dx==0&&dy==0)return;
                    if(mod_sel_c>0){
                        undo_save_ex("Center on Stage");
                        for(int i=0;i<g_no;i++){
                            if(!g_sel_flags[i])continue;
                            g_obj[i].depth+=dx;
                            g_obj[i].sy+=dy;
                        }
                        int moved_modules=module_selection_translate(dx,dy);
                        if(sel_c>0||moved_modules>0){
                            g_dirty=1; g_need_rebuild=1; g_view_changed=1;
                        }
                    } else {
                        ObjectPositionUndoCapture undo;
                        if(!object_position_undo_capture_selected(&undo))return;
                        for(int i=0;i<g_no;i++){
                            if(!g_sel_flags[i])continue;
                            g_obj[i].depth+=dx;
                            g_obj[i].sy+=dy;
                        }
                        if(object_position_undo_commit(&undo, "Center on Stage") > 0) g_need_rebuild=1;
                    }
                };
                ImGui::SameLine(0, 6);
                char ctr_tip[64];
                snprintf(ctr_tip, sizeof ctr_tip, "Center selection/highlighted modules horizontally  (stage %d px wide)", stg_w2);
                if (tb_button("Ctr H", false, ctr_tip)) {
                    do_center_toolbar(true);
                } ImGui::SameLine(0,2);
                snprintf(ctr_tip, sizeof ctr_tip, "Center selection/highlighted modules vertically  (stage %d px tall)", stg_h2);
                if (tb_button("Ctr V", false, ctr_tip)) {
                    do_center_toolbar(false);
                }
            }
        }
        snprintf(zbuf, sizeof zbuf, "%dx", g_zoom);
        /* Pin the zoom readout to the top-right via the draw list so it never
           extends the content rect (which would make the strip scrollable and
           clip the text when the buttons fill the available width). */
        ImVec2 tsz  = ImGui::CalcTextSize(zbuf);
        ImVec2 wpos = ImGui::GetWindowPos();
        ImVec2 tp(wpos.x + ImGui::GetWindowWidth() - tsz.x - 10.0f,
                  wpos.y + (ImGui::GetWindowHeight() - tsz.y) * 0.5f);
        /* only show the zoom readout when the buttons don't reach it, so it
           never overlaps the last button on a narrow window */
        if (tp.x > ImGui::GetItemRectMax().x + 8.0f)
            ImGui::GetWindowDrawList()->AddText(tp, ImGui::GetColorU32(ImGuiCol_TextDisabled), zbuf);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

/* ---- left toolbox ------------------------------------------------- */

void ToolboxPanel::render()
{
    /* Tool buttons are now in ToolbarPanel; only show the image picker
       sub-panel when Place or Brush tool is active. */
    if (g_preview_mode) return;
    if (g_cur_tool != 1 && g_cur_tool != 4) return;  /* hide when not needed */
    /* floating image-picker: appears near bottom-left when Place/Brush is active */
    ImGui::SetNextWindowPos(ImVec2(4, ImGui::GetIO().DisplaySize.y - 140), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(180, 130), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.95f));
    ImGui::Begin("##imgpick", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);

    ImGui::TextColored(ImVec4(0.6f,0.9f,1.0f,1.0f),
                       g_cur_tool == 1 ? "Place Image" : "Brush Image");
    char pi_preview[40] = "(none)";
    if (g_place_tool_img >= 0 && g_place_tool_img < g_ni) {
        if (g_img[g_place_tool_img].label[0])
            snprintf(pi_preview, sizeof pi_preview, "%.24s", g_img[g_place_tool_img].label);
        else if (g_simple_mode)
            snprintf(pi_preview, sizeof pi_preview, "Img %d  (%dx%d)",
                     g_img[g_place_tool_img].idx, g_img[g_place_tool_img].w, g_img[g_place_tool_img].h);
        else
            snprintf(pi_preview, sizeof pi_preview, "0x%02X  %dx%d",
                     g_img[g_place_tool_img].idx, g_img[g_place_tool_img].w, g_img[g_place_tool_img].h);
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##pi", pi_preview)) {
        for (int ii = 0; ii < g_ni; ii++) {
            char lbl[96];
            if (g_img[ii].label[0])
                snprintf(lbl, sizeof lbl, "%s  0x%02X  %dx%d",
                         g_img[ii].label, g_img[ii].idx, g_img[ii].w, g_img[ii].h);
            else if (g_simple_mode)
                snprintf(lbl, sizeof lbl, "Img %d  (%dx%d)", g_img[ii].idx, g_img[ii].w, g_img[ii].h);
            else
                snprintf(lbl, sizeof lbl, "0x%02X  %dx%d", g_img[ii].idx, g_img[ii].w, g_img[ii].h);
            if (ImGui::Selectable(lbl, ii == g_place_tool_img)) g_place_tool_img = ii;
        }
        ImGui::EndCombo();
    }
    if (SDL_Texture *tex = editor_texture_at(g_place_tool_img)) {
        float tw = (float)g_img[g_place_tool_img].w;
        float th = (float)g_img[g_place_tool_img].h;
        float scale = (tw > 0 && th > 0) ? ((64.0f/tw < 64.0f/th) ? 64.0f/tw : 64.0f/th) : 1.0f;
        draw_editor_texture_transparent(tex, tw * scale, th * scale);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}
