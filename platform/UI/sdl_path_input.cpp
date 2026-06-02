#include "UI/sdl_path_input.h"

#include "UI/sdl_bitmap_font.h"
#include <cstdio>
#include <cstring>

static int  g_path_input_open = 0;
static char g_path_input_buf[512] = "";
static int  g_path_input_len = 0;

int bdd_path_input_is_open(void)
{
    return g_path_input_open;
}

void bdd_path_input_open(void)
{
    g_path_input_open = 1;
    g_path_input_buf[0] = '\0';
    g_path_input_len = 0;
    SDL_StartTextInput();
}

void bdd_path_input_close(void)
{
    g_path_input_open = 0;
    SDL_StopTextInput();
}

void bdd_path_input_backspace(void)
{
    if (g_path_input_len <= 0) return;
    g_path_input_buf[--g_path_input_len] = '\0';
}

void bdd_path_input_append_text(const char *text)
{
    if (!text) return;
    int add = (int)strlen(text);
    if (g_path_input_len + add >= (int)sizeof(g_path_input_buf) - 1)
        return;
    memcpy(g_path_input_buf + g_path_input_len, text, (size_t)add);
    g_path_input_len += add;
    g_path_input_buf[g_path_input_len] = '\0';
}

const char *bdd_path_input_text(void)
{
    return g_path_input_buf;
}

void bdd_path_input_draw(SDL_Renderer *rend, int ww, int wh)
{
    int bw = 480, bh = 52;
    int bx = (ww - bw) / 2, by = (wh - bh) / 2;

    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = { bx, by, bw, bh };
    SDL_SetRenderDrawColor(rend, 14, 14, 26, 245);
    SDL_RenderFillRect(rend, &bg);
    SDL_SetRenderDrawColor(rend, 180, 180, 80, 255);
    SDL_RenderDrawRect(rend, &bg);

    SDL_Surface *surf = SDL_CreateRGBSurface(0, bw - 8, 38, 32,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (!surf) return;
    SDL_FillRect(surf, NULL, 0);
    Uint32 lc = SDL_MapRGBA(surf->format, 180, 180, 100, 255);
    Uint32 tc = SDL_MapRGBA(surf->format, 220, 220, 255, 255);
    bdd_sdl_font_draw_str(surf, 0, 0, "Load TGA - enter path and press Enter:", lc);

    char display[520];
    snprintf(display, sizeof display, "%s", g_path_input_buf);
    if ((SDL_GetTicks() / 500) % 2 == 0) {
        int len = (int)strlen(display);
        if (len < (int)sizeof(display) - 1) {
            display[len] = '_';
            display[len + 1] = '\0';
        }
    }
    bdd_sdl_font_draw_str(surf, 0, 14, display, tc);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(rend, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_Rect dst = { bx + 4, by + 7, bw - 8, 38 };
    SDL_RenderCopy(rend, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}
