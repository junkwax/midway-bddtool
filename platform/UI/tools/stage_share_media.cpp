/* Renders the picture side of a shared stage bundle: the arena views, every
   prop sprite, and the scrolling animation.
   Split from stage_share_bundle.cpp so the page generator stays readable. */

#include "UI/tools/stage_share_media.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/image_lookup.h"
#include "Core/path_utils.h"
#include "Core/stage_paths.h"
#include "libs/stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */

bool share_media_exe_path(char *out, size_t outsz)
{
    if (!out || outsz == 0) return false;
    out[0] = '\0';
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)outsz);
    return n > 0 && n < outsz;
#else
    ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
    if (n <= 0) return false;
    out[n] = '\0';
    return true;
#endif
}

/* ffmpeg is optional -- without it the bundle simply carries no animation. */
bool share_media_have_ffmpeg(void)
{
#ifdef _WIN32
    return system("ffmpeg -version >nul 2>&1") == 0;
#else
    return system("ffmpeg -version >/dev/null 2>&1") == 0;
#endif
}

static int run_quiet(const char *cmd)
{
#ifdef _WIN32
    /* Keep the console window from flashing for each frame batch. */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof pi);
    char *mutable_cmd = _strdup(cmd);
    if (!mutable_cmd) return -1;
    BOOL ok = CreateProcessA(NULL, mutable_cmd, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(mutable_cmd);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)rc;
#else
    return system(cmd);
#endif
}

bool share_media_render_game_view(const char *stage_path, const char *out_png)
{
    char exe[600];
    if (!stage_path || !stage_path[0] || !out_png || !share_media_exe_path(exe, sizeof exe))
        return false;
    char cmd[2000];
    snprintf(cmd, sizeof cmd, "\"%s\" --render-png \"%s\" \"%s\" game 1",
             exe, stage_path, out_png);
    return run_quiet(cmd) == 0 && stage_file_exists(out_png);
}

/* ------------------------------------------------------------------ */
/* props                                                               */
/* ------------------------------------------------------------------ */

int share_media_export_props(const char *props_dir, SharePropInfo *out, int max_out)
{
    if (!props_dir || !ensure_dir_recursive(props_dir)) return 0;

    int written = 0;
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (!im->pix || im->w <= 0 || im->h <= 0) continue;
        const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals)
                            ? g_pals[im->pal_idx] : NULL;
        if (!pal) continue;

        unsigned char *buf = (unsigned char *)calloc((size_t)im->w * im->h, 4);
        if (!buf) continue;
        for (int y = 0; y < im->h; y++) {
            for (int x = 0; x < im->w; x++) {
                Uint8 v = im->pix[(size_t)y * im->w + x];
                size_t off = ((size_t)y * im->w + x) * 4;
                if (!v) continue;                /* index 0 is transparent */
                Uint32 c = pal[v];
                buf[off + 0] = (c >> 16) & 0xFF;
                buf[off + 1] = (c >> 8) & 0xFF;
                buf[off + 2] = c & 0xFF;
                buf[off + 3] = 0xFF;
            }
        }

        char file[64];
        snprintf(file, sizeof file, "%03d_%02X.png", i, im->idx);
        char path[700];
        path_join(path, sizeof path, props_dir, file);
        if (stbi_write_png(path, im->w, im->h, 4, buf, im->w * 4)) {
            if (out && written < max_out) {
                snprintf(out[written].file, sizeof out[written].file, "%s", file);
                out[written].idx = im->idx;
                out[written].w = im->w;
                out[written].h = im->h;
                out[written].pal = im->pal_idx;
            }
            written++;
        }
        free(buf);
    }
    return written;
}

/* ------------------------------------------------------------------ */
/* scrolling animation                                                 */
/* ------------------------------------------------------------------ */

bool share_media_render_scroll(const char *out_dir, const char *stage_path,
                               int frames, char *gif_out, size_t gif_sz,
                               char *mp4_out, size_t mp4_sz)
{
    if (gif_out && gif_sz) gif_out[0] = '\0';
    if (mp4_out && mp4_sz) mp4_out[0] = '\0';
    if (!out_dir || !stage_path || !stage_path[0] || frames < 2) return false;

    char exe[600];
    if (!share_media_exe_path(exe, sizeof exe)) return false;
    if (!share_media_have_ffmpeg()) return false;

    char frames_dir[700];
    path_join(frames_dir, sizeof frames_dir, out_dir, "_frames");

    char cmd[2400];
    snprintf(cmd, sizeof cmd, "\"%s\" --render-scroll \"%s\" \"%s\" %d 1",
             exe, stage_path, frames_dir, frames);
    if (run_quiet(cmd) != 0)
        return false;

    char gif[700], mp4[700];
    path_join(gif, sizeof gif, out_dir, "scroll.gif");
    path_join(mp4, sizeof mp4, out_dir, "scroll.mp4");

    /* The rendered sweep is one-way; splitting it against its own reverse makes
       the animation travel out and back, and the smoothstep easing in the
       renderer keeps each turnaround gentle rather than a hard bounce.

       GIF and MP4 want opposite things here. GIF cost is roughly linear in
       frames x pixels, and a wiki page should not carry a multi-megabyte image,
       so the GIF takes every 4th frame at 320px wide with no dithering (~1.8MB
       for a 5s loop). The MP4 keeps every frame at full size and is still an
       order of magnitude smaller, so it is the one to watch for a smooth pan. */
    snprintf(cmd, sizeof cmd,
             "ffmpeg -y -loglevel error -framerate 15 -i \"%s/frame_%%04d.png\" "
             "-filter_complex \"[0:v]select='not(mod(n\\,4))',setpts=N/15/TB,"
             "scale=320:-2:flags=lanczos,"
             "split[a][b];[b]reverse[r];[a][r]concat=n=2:v=1:a=0,"
             "split[x][y];[x]palettegen=max_colors=96:stats_mode=diff[p];"
             "[y][p]paletteuse=dither=none\" \"%s\"",
             frames_dir, gif);
    bool gif_ok = run_quiet(cmd) == 0 && stage_file_exists(gif);

    snprintf(cmd, sizeof cmd,
             "ffmpeg -y -loglevel error -framerate 30 -i \"%s/frame_%%04d.png\" "
             "-filter_complex \"[0:v]split[a][b];[b]reverse[r];[a][r]concat=n=2:v=1:a=0,"
             "format=yuv420p\" -c:v libx264 -crf 20 -preset medium -movflags +faststart \"%s\"",
             frames_dir, mp4);
    bool mp4_ok = run_quiet(cmd) == 0 && stage_file_exists(mp4);

    /* Frames are an intermediate -- they would dwarf the bundle. */
    for (int i = 0; i < frames; i++) {
        char f[760];
        snprintf(f, sizeof f, "%s/frame_%04d.png", frames_dir, i);
        remove(f);
    }
#ifdef _WIN32
    RemoveDirectoryA(frames_dir);
#else
    rmdir(frames_dir);
#endif

    if (gif_ok && gif_out) snprintf(gif_out, gif_sz, "%s", gif);
    if (mp4_ok && mp4_out) snprintf(mp4_out, mp4_sz, "%s", mp4);
    return gif_ok || mp4_ok;
}
