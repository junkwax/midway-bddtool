#include "UI/app_icon.h"

#include "stb_image.h"
#include <cstdio>

static int try_set_app_icon(SDL_Window *win, const char *path)
{
    int w = 0, h = 0, channels = 0;
    unsigned char *rgba;
    SDL_Surface *surf;

    if (!win || !path || !path[0])
        return 0;

    rgba = stbi_load(path, &w, &h, &channels, 4);
    if (!rgba || w <= 0 || h <= 0)
        return 0;

    surf = SDL_CreateRGBSurfaceFrom(rgba, w, h, 32, w * 4,
                                    0x000000ff, 0x0000ff00,
                                    0x00ff0000, 0xff000000);
    if (surf) {
        SDL_SetWindowIcon(win, surf);
        SDL_FreeSurface(surf);
    }
    stbi_image_free(rgba);
    return surf != NULL;
}

void bdd_set_app_icon(SDL_Window *win)
{
    char path[512];
    char *base = SDL_GetBasePath();

    if (base) {
        snprintf(path, sizeof path, "%sassets/appicon.png", base);
        if (try_set_app_icon(win, path)) {
            SDL_free(base);
            return;
        }
        SDL_free(base);
    }

    if (try_set_app_icon(win, "assets/appicon.png")) return;
    if (try_set_app_icon(win, "appicon.png")) return;
    try_set_app_icon(win, "platform/assets/appicon.png");
}
