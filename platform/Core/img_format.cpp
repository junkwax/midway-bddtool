#include "Core/img_format.h"

#include <stdlib.h>
#include <string.h>

long img_file_size_for_import(FILE *f)
{
    long cur = ftell(f);
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long sz = ftell(f);
    fseek(f, cur, SEEK_SET);
    return sz;
}

int img_s16(unsigned short v)
{
    return (v & 0x8000u) ? (int)v - 0x10000 : (int)v;
}

void img_basename_no_ext_upper(const char *path, char *out, size_t outsz)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(out, outsz, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    for (char *p = out; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
}

void img_raw_name_to_upper(const char *raw, int raw_len, const char *fallback,
                           char *out, size_t outsz)
{
    int n = 0;
    while (n < raw_len && raw[n]) n++;
    if (n <= 0) {
        snprintf(out, outsz, "%s", fallback ? fallback : "IMG");
    } else {
        size_t o = 0;
        for (int i = 0; i < n && o + 1 < outsz; i++) {
            unsigned char ch = (unsigned char)raw[i];
            if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
            out[o++] = (char)((ch >= 32 && ch < 127) ? ch : '_');
        }
        out[o] = '\0';
    }
}

Uint32 img_pal_word_to_argb_opaque(unsigned short c)
{
    int r = (c >> 10) & 31;
    int g = (c >> 5) & 31;
    int b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

Uint32 img_pal_word_to_argb(unsigned short c, int index)
{
    if (index == 0) return 0;
    return img_pal_word_to_argb_opaque(c);
}

int img_decode_pixels(FILE *f, long file_sz, const ImgImageDisk *id,
                      int w, int h, unsigned char *dst,
                      const unsigned char *pix_map,
                      int *visible_zero_remaps)
{
    if (!f || !id || !dst || w <= 0 || h <= 0) return 0;
    if ((long)id->oset < 0 || (long)id->oset >= file_sz) return 0;
    if (fseek(f, (long)id->oset, SEEK_SET) != 0) return 0;

    if (id->flags & 0x0080) {
        int lm_mult = 1 << ((id->flags >> 8) & 3);
        int tm_mult = 1 << ((id->flags >> 10) & 3);
        for (int y = 0; y < h; y++) {
            int comp = fgetc(f);
            if (comp == EOF) return 0;
            int leading = (comp & 0x0F) * lm_mult;
            int trailing = ((comp >> 4) & 0x0F) * tm_mult;
            if (leading > w) leading = w;
            if (trailing > w - leading) trailing = w - leading;
            int visible = w - leading - trailing;
            unsigned char *row = dst + (size_t)y * (size_t)w;
            memset(row, 0, (size_t)w);
            for (int x = 0; x < visible; x++) {
                int src = fgetc(f);
                if (src == EOF) return 0;
                if (visible_zero_remaps && src == 0 && pix_map)
                    (*visible_zero_remaps)++;
                row[leading + x] = pix_map ? pix_map[(unsigned char)src] : (unsigned char)src;
            }
        }
        return 1;
    }

    int stride = (w + 3) & ~3;
    if ((long)id->oset + (long)stride * (long)h > file_sz) return 0;
    unsigned char *row = (unsigned char *)malloc((size_t)stride);
    if (!row) return 0;
    for (int y = 0; y < h; y++) {
        if (fread(row, 1, (size_t)stride, f) != (size_t)stride) {
            free(row);
            return 0;
        }
        unsigned char *out = dst + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            if (visible_zero_remaps && row[x] == 0 && pix_map)
                (*visible_zero_remaps)++;
            out[x] = pix_map ? pix_map[row[x]] : row[x];
        }
    }
    free(row);
    return 1;
}
