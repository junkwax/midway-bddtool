#include "UI/sdl_keyboard_actions.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "UI/sdl_object_picker.h"
#include "UI/object_position_undo.h"
#include "UI/sdl_path_input.h"
#include "UI/sdl_save_popup.h"
#include "UI/sdl_tga_file_dialog.h"
#include "UI/sdl_tooltip.h"
#include "UI/texture_cache.h"

#include <cstdio>

#define BDD_KR_DELAY_MS 400
#define BDD_KR_RATE_MS 50

static void save_current_stage_from_keyboard(const char *reason)
{
    int saved_bdb = bdb_save(g_bdb_path);
    int saved_bdd = g_bdd_path[0] ? bdd_save() : 1;
    if (saved_bdb && saved_bdd) {
        g_dirty = 0;
        if (reason && reason[0] == 'c')
            bdd_save_popup_cancel();
    } else {
        if (reason && reason[0] == 'c')
            fprintf(stderr, "save: keeping confirm dialog because save failed\n");
        else
            fprintf(stderr, "save: keeping dirty flag because save failed\n");
        bdd_save_logf("%s save failed: saved_bdb=%d bdb=\"%s\" saved_bdd=%d bdd=\"%s\"",
                      reason ? reason : "keyboard",
                      saved_bdb, g_bdb_path, saved_bdd, g_bdd_path);
    }
}

static void import_tga_and_refresh(const char *path, Uint64 *texture_sig)
{
    int old_ni;
    if (!path || !path[0]) return;
    old_ni = g_ni;
    if (bdd_import_tga(path) && g_ni > old_ni) {
        bdd_object_picker_free_labels();
        bdd_texture_cache_rebuild_all();
        if (texture_sig)
            *texture_sig = bdd_texture_cache_signature();
    }
}

static int any_selected_object(void)
{
    for (int si = 0; si < g_no; si++)
        if (g_sel_flags[si])
            return 1;
    return 0;
}

static ObjectPositionUndoCapture s_nudge_capture;
static int s_nudge_active = 0;
static int s_nudge_moved = 0;

static int is_nudge_key(SDL_Keycode sym)
{
    return sym == SDLK_LEFT || sym == SDLK_RIGHT ||
           sym == SDLK_UP || sym == SDLK_DOWN;
}

static void finish_keyboard_nudge(void)
{
    if (s_nudge_active && s_nudge_moved) {
        object_position_undo_commit(&s_nudge_capture, "Nudge");
    }
    s_nudge_active = 0;
    s_nudge_moved = 0;
    s_nudge_capture = ObjectPositionUndoCapture();
}

static int begin_keyboard_nudge(void)
{
    if (!any_selected_object() ||
        !object_position_undo_capture_selected(&s_nudge_capture))
        return 0;
    s_nudge_active = 1;
    s_nudge_moved = 0;
    return 1;
}

static void nudge_selected_objects(int dx, int dy, const SDL_KeyboardEvent *key)
{
    if (!key || !key->repeat) {
        finish_keyboard_nudge();
        if (!begin_keyboard_nudge())
            return;
    } else if (!s_nudge_active && !begin_keyboard_nudge()) {
        return;
    }
    for (int si = 0; si < g_no; si++) {
        if (!g_sel_flags[si]) continue;
        g_obj[si].depth += dx;
        g_obj[si].sy += dy;
        s_nudge_moved = 1;
    }
    g_dirty = 1;
}

void bdd_sdl_keyboard_init(BddSdlKeyboardState *state)
{
    if (!state) return;
    state->repeat_sym = SDLK_UNKNOWN;
    state->repeat_next = 0;
}

void bdd_sdl_keyboard_key_down(BddSdlKeyboardState *state,
                               const SDL_KeyboardEvent *key,
                               SDL_Renderer *rend,
                               int *view_x, int *view_y, int *zoom,
                               int wx_min, int wy_min,
                               Uint64 *texture_sig,
                               int *running)
{
    SDL_Keycode sym;
    SDL_Keymod mod;

    (void)rend;
    if (!key || !view_x || !view_y || !zoom) return;

    sym = key->keysym.sym;
    mod = (SDL_Keymod)key->keysym.mod;

    if (state && !key->repeat) {
        state->repeat_sym = sym;
        state->repeat_next = SDL_GetTicks() + BDD_KR_DELAY_MS;
    }

    if (bdd_path_input_is_open()) {
        if (sym == SDLK_BACKSPACE) {
            bdd_path_input_backspace();
        } else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
            const char *input_path = bdd_path_input_text();
            bdd_path_input_close();
            import_tga_and_refresh(input_path, texture_sig);
        } else if (sym == SDLK_ESCAPE) {
            bdd_path_input_close();
        }
        return;
    }

    switch (sym) {
    case SDLK_LEFT:
        if (any_selected_object() && g_have_bdb) {
            int step = (SDL_GetModState() & KMOD_SHIFT) ? g_grid_sx : 1;
            nudge_selected_objects(-step, 0, key);
        } else {
            *view_x -= 64 / *zoom;
        }
        break;
    case SDLK_RIGHT:
        if (any_selected_object() && g_have_bdb) {
            int step = (SDL_GetModState() & KMOD_SHIFT) ? g_grid_sx : 1;
            nudge_selected_objects(step, 0, key);
        } else {
            *view_x += 64 / *zoom;
        }
        break;
    case SDLK_UP:
        if (any_selected_object() && g_have_bdb) {
            int step = (SDL_GetModState() & KMOD_SHIFT) ? g_grid_sy : 1;
            nudge_selected_objects(0, -step, key);
        } else {
            *view_y -= 32 / *zoom;
        }
        break;
    case SDLK_DOWN:
        if (any_selected_object() && g_have_bdb) {
            int step = (SDL_GetModState() & KMOD_SHIFT) ? g_grid_sy : 1;
            nudge_selected_objects(0, step, key);
        } else {
            *view_y += 32 / *zoom;
        }
        break;
    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS:
        if (*zoom < 8) (*zoom)++;
        break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        if (*zoom > 1) (*zoom)--;
        break;
    case SDLK_HOME:
        *view_x = wx_min;
        *view_y = wy_min;
        *zoom = 1;
        break;
    case SDLK_s:
        if ((mod & KMOD_CTRL) && g_have_bdb && !g_confirm_save)
            save_current_stage_from_keyboard("Ctrl+S");
        break;
    case SDLK_y:
        if (!(mod & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI)) && g_confirm_save)
            save_current_stage_from_keyboard("confirm");
        break;
    case SDLK_n:
        if (g_confirm_save)
            bdd_save_popup_cancel();
        break;
    case SDLK_l:
        if ((mod & KMOD_CTRL) && !bdd_path_input_is_open()) {
            char dlg_path[512] = "";
            if (bdd_sdl_open_tga_file_dialog(dlg_path, sizeof dlg_path))
                import_tga_and_refresh(dlg_path, texture_sig);
            else
                bdd_path_input_open();
        }
        break;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        break;
    case SDLK_TAB:
        if (g_have_bdb) {
            bdd_object_picker_toggle();
            bdd_object_picker_cancel_place();
        }
        break;
    case SDLK_ESCAPE:
        if (bdd_path_input_is_open()) {
            bdd_path_input_close();
        } else if (g_confirm_save) {
            bdd_save_popup_cancel();
        } else if (g_place_img >= 0) {
            bdd_object_picker_cancel_place();
        } else if (bdd_object_picker_is_open()) {
            bdd_object_picker_close();
        } else if (running && bg_editor_request_close()) {
            *running = 0;
        }
        break;
    case SDLK_t:
        if (mod & KMOD_SHIFT) g_show_grid ^= 1;
        break;
    case SDLK_b:
        if (mod & KMOD_SHIFT) g_show_borders ^= 1;
        break;
    case SDLK_o:
        if (mod & KMOD_SHIFT) {
            g_show_objects ^= 1;
        } else {
            g_show_labels ^= 1;
            if (!g_show_labels) bdd_tooltip_free();
        }
        break;
    default:
        break;
    }
}

void bdd_sdl_keyboard_key_up(BddSdlKeyboardState *state,
                             const SDL_KeyboardEvent *key)
{
    if (!state || !key) return;
    if (is_nudge_key(key->keysym.sym))
        finish_keyboard_nudge();
    if (key->keysym.sym == state->repeat_sym)
        state->repeat_sym = SDLK_UNKNOWN;
}

void bdd_sdl_keyboard_text_input(const SDL_TextInputEvent *text)
{
    if (text && bdd_path_input_is_open())
        bdd_path_input_append_text(text->text);
}

void bdd_sdl_keyboard_tick(BddSdlKeyboardState *state,
                           int *view_x, int *view_y, int zoom)
{
    Uint32 now;

    if (!state || !view_x || !view_y || state->repeat_sym == SDLK_UNKNOWN)
        return;
    now = SDL_GetTicks();
    if (now < state->repeat_next)
        return;

    state->repeat_next = now + BDD_KR_RATE_MS;
    switch (state->repeat_sym) {
    case SDLK_LEFT:  *view_x -= 64 / zoom; break;
    case SDLK_RIGHT: *view_x += 64 / zoom; break;
    case SDLK_UP:    *view_y -= 32 / zoom; break;
    case SDLK_DOWN:  *view_y += 32 / zoom; break;
    default: break;
    }
}
