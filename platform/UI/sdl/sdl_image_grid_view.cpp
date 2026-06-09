#include "UI/sdl/sdl_image_grid_view.h"

#include "UI/assets/texture_cache.h"
#include "bg_editor_globals.h"

void bdd_image_grid_view_draw(SDL_Renderer *rend, int win_w,
                              int scroll_x, int scroll_y, int zoom)
{
    int pad = 8 * zoom;
    int cx = -scroll_x;
    int cy = -scroll_y;
    int row_h = 0;

    for (int i = 0; i < g_ni; i++) {
        if (!g_textures || !g_textures[i]) continue;
        Img *im = &g_img[i];
        int tw = im->w * zoom;
        int th = im->h * zoom;

        if (cx + tw + pad > win_w) {
            cx = -scroll_x;
            cy += row_h + pad;
            row_h = 0;
        }
        if (row_h < th) row_h = th;

        SDL_Rect dst = { cx, cy, tw, th };
        SDL_RenderCopy(rend, g_textures[i], NULL, &dst);

        SDL_SetRenderDrawColor(rend, 80, 80, 100, 255);
        SDL_RenderDrawRect(rend, &dst);

        cx += tw + pad;
    }
}
