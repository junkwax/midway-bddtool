#include "Core/tga_import.h"

#include "bg_editor.h"
#include "Core/bdd_core.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/viewer_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TGA loader (type 1: 8-bit paletted with RGB555 colour map) */
static int tga_load(const char *path, int *out_w, int *out_h,
                    Uint8 **out_pix, Uint16 **out_pal, int *out_pal_cnt)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "tga: cannot open %s\n", path);
        return 0;
    }

    Uint8 hdr[18];
    if (fread(hdr, 1, 18, f) < 18) {
        fclose(f);
        return 0;
    }

    int id_len = hdr[0];
    int cmap_type = hdr[1];
    int img_type = hdr[2];
    int cmap_count = (hdr[6] << 8) | hdr[5];
    int cmap_depth = hdr[7];
    int width = (hdr[13] << 8) | hdr[12];
    int height = (hdr[15] << 8) | hdr[14];
    int pix_depth = hdr[16];
    int img_desc = hdr[17];

    if (img_type != 1 || pix_depth != 8 || cmap_type != 1) {
        fprintf(stderr, "tga: only 8-bit paletted TGA supported (got type=%d depth=%d)\n",
                img_type, pix_depth);
        fclose(f);
        return 0;
    }

    fseek(f, id_len, SEEK_CUR);

    int bpp = (cmap_depth + 7) / 8;
    Uint16 *pal = (Uint16 *)malloc((size_t)cmap_count * sizeof(Uint16));
    if (!pal) {
        fclose(f);
        return 0;
    }
    for (int i = 0; i < cmap_count; i++) {
        Uint8 buf[4] = {0};
        if (fread(buf, 1, (size_t)bpp, f) != (size_t)bpp) {
            free(pal);
            fclose(f);
            return 0;
        }
        pal[i] = (Uint16)(buf[0] | (buf[1] << 8));
    }

    int npix = width * height;
    Uint8 *raw = (Uint8 *)malloc((size_t)npix);
    Uint8 *pix = (Uint8 *)malloc((size_t)npix);
    if (!raw || !pix) {
        free(raw);
        free(pix);
        free(pal);
        fclose(f);
        return 0;
    }
    if ((int)fread(raw, 1, (size_t)npix, f) < npix) {
        free(raw);
        free(pix);
        free(pal);
        fclose(f);
        return 0;
    }
    fclose(f);

    /* Flip vertically if origin is bottom-left (img_desc bit 5 = 0). */
    if (img_desc & 0x20) {
        memcpy(pix, raw, (size_t)npix);
    } else {
        for (int row = 0; row < height; row++)
            memcpy(pix + row * width, raw + (height - 1 - row) * width, (size_t)width);
    }
    free(raw);

    *out_w = width;
    *out_h = height;
    *out_pix = pix;
    *out_pal = pal;
    *out_pal_cnt = cmap_count;
    return 1;
}

int bdd_import_tga(const char *tga_path)
{
    if (!editor_project_storage_init()) return 0;
    if (!g_bdd_path[0]) {
        fprintf(stderr, "tga: load a BDD first\n");
        return 0;
    }
    if (!editor_project_reserve_images(g_ni + 1)) {
        fprintf(stderr, "tga: image reserve failed\n");
        return 0;
    }
    if (!editor_project_reserve_palettes(g_n_pals + 1)) {
        fprintf(stderr, "tga: palette reserve failed\n");
        return 0;
    }

    int w, h, pal_cnt;
    Uint8 *pix = NULL;
    Uint16 *pal = NULL;
    if (!tga_load(tga_path, &w, &h, &pix, &pal, &pal_cnt)) return 0;

    bg_editor_set_action_label("Import");
    bg_editor_undo_save();

    int max_idx = 0;
    for (int i = 0; i < g_ni; i++) {
        if (g_img[i].idx > max_idx)
            max_idx = g_img[i].idx;
    }
    int new_idx = max_idx + 1;

    const char *base = tga_path;
    for (const char *s = tga_path; *s; s++) {
        if (*s == '/' || *s == '\\')
            base = s + 1;
    }
    char pal_name[64];
    snprintf(pal_name, sizeof pal_name, "%s", base);
    char *dot = strrchr(pal_name, '.');
    if (dot) *dot = '\0';
    for (char *p = pal_name; *p; p++) {
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
    }

    Uint32 colors[256];
    for (int i = 0; i < pal_cnt; i++) {
        Uint16 c = pal[i];
        colors[i] = (i == 0) ? 0 : bdd_core_rgb555_to_argb(c);
    }
    for (int i = pal_cnt; i < 256; i++) {
        colors[i] = 0xFF000000u;
    }

    int pi = editor_project_append_palette_slot(pal_name, pal_cnt, colors);
    if (pi < 0) {
        free(pal);
        free(pix);
        fprintf(stderr, "tga: palette append failed\n");
        return 0;
    }
    editor_project_set_palette_rgb555_cache(pi, pal, pal_cnt);
    free(pal);

    Img *im = editor_project_append_image_slot();
    if (!im) {
        free(pix);
        editor_project_truncate_palettes(pi);
        fprintf(stderr, "tga: image append failed\n");
        return 0;
    }
    im->idx = new_idx;
    im->w = w;
    im->h = h;
    im->flags = 0;
    im->pal_idx = pi;
    snprintf(im->label, sizeof im->label, "%s", g_pal_name[pi]);
    snprintf(im->source, sizeof im->source, "%s", base);
    im->pix = pix;

    bdd_save();

    fprintf(stderr, "tga: imported %s as idx=0x%02X  pal=%d  %dx%d\n",
            tga_path, new_idx, pi, w, h);
    return 1;
}
