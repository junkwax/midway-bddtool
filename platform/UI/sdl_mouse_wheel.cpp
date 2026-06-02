#include "UI/sdl_mouse_wheel.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/editor_project_storage.h"
#include "UI/sdl_object_picker.h"
#include "UI/sdl_tooltip.h"
#include "UI/texture_cache.h"

void bdd_sdl_mouse_wheel_handle(const SDL_MouseWheelEvent *wheel,
                                SDL_Renderer *rend,
                                int wh, int ww,
                                int hover_x, int hover_y,
                                int *view_x, int *view_y,
                                int *zoom,
                                int *last_obj,
                                int *hover_printed)
{
    if (!wheel || !view_x || !view_y || !zoom || !last_obj)
        return;

    if (bdd_object_picker_is_open()) {
        bdd_object_picker_scroll(wheel->y, wh);
    } else if (SDL_GetModState() & KMOD_CTRL) {
        if (*last_obj >= 0 && *last_obj < g_no) {
            int saved_order;
            Obj *o;
            int hi;

            bg_editor_set_action_label("Layer Change");
            bg_editor_undo_save();
            saved_order = g_obj[*last_obj].order;
            o = &g_obj[*last_obj];
            hi = (o->wx >> 8) & 0xFF;
            hi = (wheel->y > 0) ? (hi + 1) & 0xFF : (hi - 1) & 0xFF;
            o->wx = (hi << 8) | (o->wx & 0xFF);
            editor_project_sort_objects_by_layer_order();
            g_dirty = 1;
            g_need_rebuild = 1;
            for (int i = 0; i < g_no; i++) {
                if (g_obj[i].order == saved_order) {
                    *last_obj = i;
                    break;
                }
            }
            bdd_tooltip_build_object(rend, *last_obj, hover_x, hover_y, ww, wh);
            if (hover_printed) *hover_printed = 1;
        } else {
            int old_z = *zoom;
            int mx = hover_x;
            int my = hover_y;
            int wx_before = 0, wy_before = 0;
            bdd_screen_to_world(mx, my, *view_x, *view_y, old_z,
                                &wx_before, &wy_before);
            if (wheel->y > 0 && *zoom < 8) (*zoom)++;
            if (wheel->y < 0 && *zoom > 1) (*zoom)--;
            if (*zoom != old_z) {
                *view_x = wx_before - mx / *zoom;
                *view_y = wy_before - my / *zoom;
            }
        }
    } else {
        int old_z = *zoom;
        int mx = hover_x;
        int my = hover_y;
        int wx_before = 0, wy_before = 0;
        bdd_screen_to_world(mx, my, *view_x, *view_y, old_z,
                            &wx_before, &wy_before);
        if (wheel->y > 0 && *zoom < 8) (*zoom)++;
        if (wheel->y < 0 && *zoom > 1) (*zoom)--;
        if (*zoom != old_z) {
            *view_x = wx_before - mx / *zoom;
            *view_y = wy_before - my / *zoom;
        }
    }
}
