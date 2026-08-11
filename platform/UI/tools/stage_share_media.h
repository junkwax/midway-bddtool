#ifndef STAGE_SHARE_MEDIA_H
#define STAGE_SHARE_MEDIA_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char file[64];
    int idx;     /* BDD image id */
    int w, h;
    int pal;
} SharePropInfo;

/* Writes one PNG per BDD image into props_dir. Returns the count written and
   fills out[] with up to max_out descriptors. */
int share_media_export_props(const char *props_dir, SharePropInfo *out, int max_out);

/* Renders the camera sweeping the stage and encodes scroll.gif / scroll.mp4
   into out_dir. Needs ffmpeg on PATH; returns false (harmlessly) without it. */
bool share_media_render_scroll(const char *out_dir, const char *stage_path,
                               int frames, char *gif_out, size_t gif_sz,
                               char *mp4_out, size_t mp4_sz);

/* Renders the in-game 400x254 framing at the stage's start camera. */
bool share_media_render_game_view(const char *stage_path, const char *out_png);

bool share_media_have_ffmpeg(void);
bool share_media_exe_path(char *out, size_t outsz);

#endif
