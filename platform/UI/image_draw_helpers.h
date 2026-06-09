#ifndef IMAGE_DRAW_HELPERS_H
#define IMAGE_DRAW_HELPERS_H

#include <SDL.h>

SDL_Texture *editor_texture_at(int img_i);
void draw_editor_texture_transparent(SDL_Texture *tex, float width, float height);
void draw_editor_texture_transparent_uv(SDL_Texture *tex, float width, float height,
                                        float u0, float v0, float u1, float v1);

#endif
