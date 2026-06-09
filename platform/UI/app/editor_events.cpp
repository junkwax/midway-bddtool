#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/sdl/sdl_object_picker.h"
#include "undo_manager.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"

#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
void bg_editor_process_event(SDL_Event *event)
{
    if (event->type == SDL_KEYDOWN && !ImGui::GetIO().WantCaptureKeyboard) {
        bool ctrl = (event->key.keysym.mod & KMOD_CTRL) != 0;
        if (event->key.keysym.sym == SDLK_F1) {
            g_show_help = !g_show_help;
            return;
        }
        if (event->key.keysym.sym == SDLK_SPACE) {
            if (g_game_view) {
                /* Space in game preview = toggle animation playback */
                g_anim_playing = !g_anim_playing;
                if (g_anim_playing) g_anim_fpos = (float)g_scroll_pos;
            } else {
                g_preview_mode = !g_preview_mode;
            }
            return;
        }
        if (event->key.keysym.sym == SDLK_F11) {
            g_preview_mode = !g_preview_mode;
            return;
        }
        if (!ctrl && event->key.keysym.sym == SDLK_d && g_no > 0) {
            /* D key: duplicate selected objects in place */
            int max_order = 0;
            for (int i = 0; i < g_no; i++) if (g_obj[i].order > max_order) max_order = g_obj[i].order;
            int added = 0;
            for (int si = 0; si < g_no; si++) {
                if (!g_sel_flags[si]) continue;
                Obj *dst = editor_project_append_object_slot();
                if (!dst) break;
                if (!added) undo_save();
                *dst = g_obj[si];
                dst->depth += 16; dst->sy += 8;
                dst->order = max_order + 1 + added;
                added++;
            }
            if (added) { g_dirty = 1; g_need_rebuild = 1; }
            return;
        }
        /* arrow keys: game-view camera scroll (selection nudge handled by bddview.c) */
        {
            int dx = 0, dy = 0;
            if (event->key.keysym.sym == SDLK_LEFT)  dx = -1;
            if (event->key.keysym.sym == SDLK_RIGHT) dx =  1;
            if (event->key.keysym.sym == SDLK_UP)    dy = -1;
            if (event->key.keysym.sym == SDLK_DOWN)  dy =  1;
            if ((dx || dy) && g_game_view) {
                g_scroll_pos += dx;
                g_game_view_y += dy;
                return;
            }
        }
        if (ctrl && !(event->key.keysym.mod & KMOD_SHIFT) && event->key.keysym.sym == SDLK_z) {
            if (undo_is_available()) undo_restore(); return;
        }
        if (ctrl && event->key.keysym.sym == SDLK_y) {
            if (redo_is_available()) redo_restore(); return;
        }
        if (ctrl && (event->key.keysym.mod & KMOD_SHIFT) && event->key.keysym.sym == SDLK_z && g_have_bdb) {
            zoom_to_selection(); return;
        }
        /* Ctrl+Home: fit entire stage; Ctrl+1: zoom 1:1 */
        if (ctrl && event->key.keysym.sym == SDLK_HOME) { zoom_to_fit(); return; }
        if (ctrl && event->key.keysym.sym == SDLK_1) {
            g_zoom = 1; g_view_changed = 1; return;
        }
        if (ctrl && event->key.keysym.sym == SDLK_c && g_no > 0) {
            copy_selected_objects_to_clipboard();
            return;
        }
        if (ctrl && event->key.keysym.sym == SDLK_v && g_clip_count > 0
            && editor_project_reserve_objects(g_no + g_clip_count))
        {
            int offset = (event->key.keysym.mod & KMOD_SHIFT) ? 0 : 32;
            paste_clipboard_objects(offset, offset ? 16 : 0);
            return;
        }
        if (ctrl && event->key.keysym.sym == SDLK_x && g_no > 0) {
            /* Cut: copy to clipboard + delete */
            copy_selected_objects_to_clipboard();
            if (g_clip_count > 0)
                delete_object_targets_preserve_order(-1, "Cut");
            return;
        }
        if (ctrl && event->key.keysym.sym == SDLK_d && g_no > 0) {
            undo_save();
            int max_order = 0;
            for (int i = 0; i < g_no; i++)
                if (g_obj[i].order > max_order) max_order = g_obj[i].order;
            int added = 0;
            for (int si = 0; si < g_no; si++) {
                if (!g_sel_flags[si]) continue;
                Obj *dst = editor_project_append_object_slot();
                if (!dst) break;
                *dst = g_obj[si];
                dst->depth += 16;
                dst->sy    += 8;
                dst->order  = max_order + 1 + added;
                added++;
            }
            if (added) g_need_rebuild = 1;
            return;
        }
        if (ctrl && event->key.keysym.sym == SDLK_a && g_have_bdb && g_no > 0) {
            /* Ctrl+A: select all (or deselect all if all already selected) */
            int sel_n = 0;
            for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) sel_n++;
            int val = (sel_n == g_no) ? 0 : 1;
            for (int i = 0; i < g_no; i++) g_sel_flags[i] = val;
            return;
        }
        if (ctrl && event->key.keysym.sym == SDLK_i && g_have_bdb && g_no > 0) {
            /* Ctrl+I: invert selection */
            for (int i = 0; i < g_no; i++) g_sel_flags[i] ^= 1;
            return;
        }
        if (!ctrl && event->key.keysym.sym == SDLK_ESCAPE && !ImGui::IsPopupOpen("")) {
            /* Escape: clear selection */
            if (g_have_bdb) {
                int had = 0;
                for (int i = 0; i < g_no; i++) if (g_sel_flags[i]) { had = 1; break; }
                if (had) {
                    editor_project_clear_selection();
                    g_hl_obj = -1;
                    return;
                }
            }
        }
        if ((event->key.keysym.sym == SDLK_DELETE || event->key.keysym.sym == SDLK_BACKSPACE)
            && g_have_bdb && g_no > 0) {
            /* Delete / Backspace: delete exactly the current selection,
               preserving array order and object order fields.  Canvas and
               runtime clicks sometimes leave only the lead object active for
               one frame, so fall back to g_hl_obj instead of requiring a
               second select/delete cycle. */
            int active = (g_hl_obj >= 0 && g_hl_obj < g_no) ? g_hl_obj : -1;
            if (selected_count() > 0 || active >= 0) {
                delete_object_targets_preserve_order(active, "Delete");
                return;  /* prevent bddview.c's non-undoable delete handler */
            }
        }
        if (!ctrl && event->key.keysym.sym == SDLK_x && g_have_bdb) {
            /* X: toggle H-flip on selected objects */
            int active = (g_hl_obj >= 0 && g_hl_obj < g_no) ? g_hl_obj : -1;
            if (selected_count() > 0 || active >= 0) {
                flip_object_targets_mirrored(active, true, "H-Flip");
                return;
            }
        }
        if (!ctrl && event->key.keysym.sym == SDLK_y && g_have_bdb) {
            /* Y: toggle V-flip on selected objects */
            int active = (g_hl_obj >= 0 && g_hl_obj < g_no) ? g_hl_obj : -1;
            if (selected_count() > 0 || active >= 0) {
                flip_object_targets_mirrored(active, false, "V-Flip");
                return;
            }
        }
    }
    /* drag-drop PNG/TGA from OS file manager */
    if (event->type == SDL_DROPFILE) {
        char *dropped = event->drop.file;
        if (dropped) {
            size_t dlen = strlen(dropped);
            bool is_png = dlen >= 4 && strcasecmp(dropped + dlen - 4, ".png") == 0;
            bool is_tga = dlen >= 4 && strcasecmp(dropped + dlen - 4, ".tga") == 0;
            bool is_bdb = dlen >= 4 && strcasecmp(dropped + dlen - 4, ".bdb") == 0;
            bool is_bdd = dlen >= 4 && strcasecmp(dropped + dlen - 4, ".bdd") == 0;
            if (is_png) {
                import_png(dropped);
            } else if (is_tga) {
                int old_ni = g_ni;
                if (bdd_import_tga(dropped) && g_ni > old_ni) {
                    bdd_object_picker_free_labels();
                    g_need_rebuild = 1; g_show_images = true; g_dirty = 1;
                }
            } else if (is_bdb || is_bdd) {
                request_unsaved_action(UNSAVED_ACTION_OPEN_STAGE, dropped);
                SDL_free(dropped);
                event->type = SDL_FIRSTEVENT;
                return;
            }
            SDL_free(dropped);
        }
        return;
    }
    ImGui_ImplSDL2_ProcessEvent(event);
}

