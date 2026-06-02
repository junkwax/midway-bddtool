#include "Core/viewer_test_levels.h"

#include "Core/bdd_core.h"
#include "bg_editor_globals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void make_ext_local(const char *src, const char *ext, char *out, size_t outsz)
{
    strncpy(out, src, outsz - 1);
    out[outsz - 1] = '\0';
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    char *backslash = strrchr(out, '\\');
    char *sep = slash > backslash ? slash : backslash;
    if (!dot || (sep && dot < sep)) dot = out + strlen(out);
    strncpy(dot, ext, outsz - (size_t)(dot - out) - 1);
}

static unsigned checker_rgb555(int r, int g, int b)
{
    Uint32 c = 0xFF000000u | ((Uint32)(Uint8)r << 16) |
               ((Uint32)(Uint8)g << 8) | (Uint8)b;
    return (unsigned)bdd_core_argb_to_rgb555(c);
}

static void proof_rect(Uint8 *pix, int w, int h, int x, int y, int rw, int rh, Uint8 color)
{
    int x2 = x + rw;
    int y2 = y + rh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > w) x2 = w;
    if (y2 > h) y2 = h;
    for (int yy = y; yy < y2; yy++)
        for (int xx = x; xx < x2; xx++)
            pix[yy * w + xx] = color;
}

static const char *proof_glyph_row(char c, int row)
{
    static const char *blank[7] = {
        "00000", "00000", "00000", "00000", "00000", "00000", "00000"
    };
    static const char *b[7] = {
        "11110", "10001", "10001", "11110", "10001", "10001", "11110"
    };
    static const char *g[7] = {
        "01111", "10000", "10000", "10111", "10001", "10001", "01111"
    };
    static const char *p_glyph[7] = {
        "11110", "10001", "10001", "11110", "10000", "10000", "10000"
    };
    static const char *r[7] = {
        "11110", "10001", "10001", "11110", "10100", "10010", "10001"
    };
    static const char *o[7] = {
        "01110", "10001", "10001", "10001", "10001", "10001", "01110"
    };
    static const char *f[7] = {
        "11111", "10000", "10000", "11110", "10000", "10000", "10000"
    };
    static const char *t[7] = {
        "11111", "00100", "00100", "00100", "00100", "00100", "00100"
    };
    static const char *s[7] = {
        "01111", "10000", "10000", "01110", "00001", "00001", "11110"
    };
    static const char *one[7] = {
        "00100", "01100", "00100", "00100", "00100", "00100", "01110"
    };
    const char **glyph = blank;
    if (c == 'B') glyph = b;
    else if (c == 'G') glyph = g;
    else if (c == 'P') glyph = p_glyph;
    else if (c == 'R') glyph = r;
    else if (c == 'O') glyph = o;
    else if (c == 'F') glyph = f;
    else if (c == 'T') glyph = t;
    else if (c == 'S') glyph = s;
    else if (c == '1') glyph = one;
    return glyph[row];
}

static void proof_text(Uint8 *pix, int w, int h, int x, int y,
                       const char *text, int scale, Uint8 color)
{
    int cursor = x;
    for (const char *p = text; *p; p++) {
        for (int row = 0; row < 7; row++) {
            const char *bits = proof_glyph_row(*p, row);
            for (int col = 0; col < 5; col++) {
                if (bits[col] == '1')
                    proof_rect(pix, w, h, cursor + col * scale, y + row * scale,
                               scale, scale, color);
            }
        }
        cursor += 6 * scale;
    }
}

static void proof_mark_tiles(Uint8 *pix, int w, int h, int tile_w, int tile_h)
{
    int cols = w / tile_w;
    int rows = (h + tile_h - 1) / tile_h;

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int idx = row * cols + col + 1;
            int x0 = col * tile_w;
            int y0 = row * tile_h;
            int cur_h = tile_h;
            if (y0 + cur_h > h) cur_h = h - y0;

            int mark_w = 10;
            int mark_h = 8;
            int mx = x0 + 4;
            int my = y0 + cur_h - mark_h - 1;
            if (mx + mark_w > x0 + tile_w) mx = x0;
            if (my < y0) my = y0;

            for (int yy = 0; yy < mark_h; yy++)
                for (int xx = 0; xx < mark_w; xx++)
                    pix[(my + yy) * w + (mx + xx)] = 1;
            for (int k = 0; k < idx && k < mark_w * mark_h; k++)
                pix[(my + k / mark_w) * w + (mx + k % mark_w)] = 8;
        }
    }
}

static void fill_bg_proof_pixels(Uint8 *pix, int w, int h)
{
    static const Uint8 bars[7] = {1, 2, 3, 4, 6, 7, 5};

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int bi = (x * 7) / w;
            pix[y * w + x] = bars[bi < 0 ? 0 : (bi > 6 ? 6 : bi)];
        }
    }

    proof_rect(pix, w, h, 0, 0, 32, 32, 1);
    proof_rect(pix, w, h, w - 32, 0, 32, 32, 2);
    proof_rect(pix, w, h, 0, h - 32, 32, 32, 3);
    proof_rect(pix, w, h, w - 32, h - 32, 32, 32, 4);

    for (int y = 36; y < h; y += 24)
        proof_rect(pix, w, h, 0, y, w, 3, 5);
    for (int x = 36; x < w; x += 40)
        proof_rect(pix, w, h, x, 0, 3, h, 8);

    proof_rect(pix, w, h, 36, 84, 328, 86, 8);
    proof_rect(pix, w, h, 42, 90, 316, 74, 6);
    proof_rect(pix, w, h, 50, 98, 300, 58, 8);
    proof_text(pix, w, h, 62, 100, "BGPROF", 8, 5);

    proof_rect(pix, w, h, 0, 0, w, 4, 5);
    proof_rect(pix, w, h, 0, h - 4, w, 4, 5);
    proof_rect(pix, w, h, 0, 0, 4, h, 5);
    proof_rect(pix, w, h, w - 4, 0, 4, h, 5);

    proof_mark_tiles(pix, w, h, 40, 32);
}

static void write_rgb555_palette(FILE *f, const unsigned *colors, int count)
{
    for (int i = 0; i < count; i++) {
        unsigned char lo = (unsigned char)(colors[i] & 0xFF);
        unsigned char hi = (unsigned char)((colors[i] >> 8) & 0xFF);
        fwrite(&lo, 1, 1, f);
        fwrite(&hi, 1, 1, f);
    }
}

int bdd_write_checker_test_level(const char *prefix)
{
    char base[512];
    char bdb[512];
    char bdd[512];
    snprintf(base, sizeof base, "%s", (prefix && prefix[0]) ? prefix : "CHECKER");
    make_ext_local(base, ".BDB", bdb, sizeof bdb);
    make_ext_local(base, ".BDD", bdd, sizeof bdd);

    FILE *fbdb = fopen(bdb, "w");
    if (!fbdb) {
        fprintf(stderr, "checker: cannot write %s\n", bdb);
        return 0;
    }
    fprintf(fbdb, "CHECKER 400 254 255 1 1 1\n");
    fprintf(fbdb, "CHKMOD 0 399 0 253\n");
    fprintf(fbdb, "4000 0 0 1 0\n");
    fclose(fbdb);

    FILE *fbdd = fopen(bdd, "wb");
    if (!fbdd) {
        fprintf(stderr, "checker: cannot write %s\n", bdd);
        return 0;
    }
    fprintf(fbdd, "1\n");
    fprintf(fbdd, "1 64 64 0\n");
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            unsigned char px = (unsigned char)(1 + (((x / 8) + (y / 8)) & 3));
            fwrite(&px, 1, 1, fbdd);
        }
    }
    fprintf(fbdd, "CHECKPAL 5\n");
    {
        unsigned colors[5] = {
            checker_rgb555(0, 0, 0),
            checker_rgb555(255, 0, 0),
            checker_rgb555(0, 255, 0),
            checker_rgb555(0, 0, 255),
            checker_rgb555(255, 255, 0),
        };
        write_rgb555_palette(fbdd, colors, 5);
    }
    fclose(fbdd);

    if (!bdd_load(bdd)) return 0;
    if (!bdb_load(bdb)) return 0;
    if (g_ni != 1 || g_no != 1 || g_n_pals != 1) {
        fprintf(stderr, "checker: reload count mismatch images=%d objects=%d palettes=%d\n",
                g_ni, g_no, g_n_pals);
        return 0;
    }
    if (g_img[0].idx != 1 || g_img[0].w != 64 || g_img[0].h != 64 ||
        g_obj[0].wx != 0x4000 || g_obj[0].depth != 0 || g_obj[0].sy != 0 ||
        g_obj[0].ii != 1 || g_obj[0].fl != 0) {
        fprintf(stderr, "checker: reload value mismatch\n");
        return 0;
    }

    fprintf(stderr, "checker: wrote and reloaded %s + %s\n", bdb, bdd);
    return 1;
}

int bdd_write_bg_proof_level(const char *prefix)
{
    enum { PROOF_W = 400, PROOF_H = 254 };
    enum { PROOF_TILE_W = 40, PROOF_TILE_H = 32 };
    enum { PROOF_COLS = PROOF_W / PROOF_TILE_W };
    enum { PROOF_ROWS = (PROOF_H + PROOF_TILE_H - 1) / PROOF_TILE_H };
    enum { PROOF_TILES = PROOF_COLS * PROOF_ROWS };
    char base[512];
    char bdb[512];
    char bdd[512];
    snprintf(base, sizeof base, "%s", (prefix && prefix[0]) ? prefix : "BGPROF");
    make_ext_local(base, ".BDB", bdb, sizeof bdb);
    make_ext_local(base, ".BDD", bdd, sizeof bdd);

    FILE *fbdb = fopen(bdb, "w");
    if (!fbdb) {
        fprintf(stderr, "bgproof: cannot write %s\n", bdb);
        return 0;
    }
    fprintf(fbdb, "BGPROF 400 254 255 1 1 %d\n", PROOF_TILES);
    fprintf(fbdb, "TSTMOD 0 399 0 253\n");
    for (int col = 0; col < PROOF_COLS; col++) {
        for (int row = 0; row < PROOF_ROWS; row++) {
            int idx = row * PROOF_COLS + col + 1;
            fprintf(fbdb, "4000 %d %d %X 0\n",
                    col * PROOF_TILE_W, row * PROOF_TILE_H, idx);
        }
    }
    fclose(fbdb);

    Uint8 *pix = (Uint8 *)malloc(PROOF_W * PROOF_H);
    if (!pix) {
        fprintf(stderr, "bgproof: out of memory\n");
        return 0;
    }
    fill_bg_proof_pixels(pix, PROOF_W, PROOF_H);

    FILE *fbdd = fopen(bdd, "wb");
    if (!fbdd) {
        free(pix);
        fprintf(stderr, "bgproof: cannot write %s\n", bdd);
        return 0;
    }
    fprintf(fbdd, "%d\n", PROOF_TILES);
    for (int row = 0; row < PROOF_ROWS; row++) {
        for (int col = 0; col < PROOF_COLS; col++) {
            int idx = row * PROOF_COLS + col + 1;
            int tile_h = PROOF_TILE_H;
            int y0 = row * PROOF_TILE_H;
            int x0 = col * PROOF_TILE_W;
            if (y0 + tile_h > PROOF_H) tile_h = PROOF_H - y0;
            fprintf(fbdd, "%X %d %d 0\n", idx, PROOF_TILE_W, tile_h);
            for (int y = 0; y < tile_h; y++)
                fwrite(pix + (y0 + y) * PROOF_W + x0, 1, PROOF_TILE_W, fbdd);
        }
    }
    fprintf(fbdd, "PROOFPAL 9\n");
    {
        unsigned colors[9] = {
            checker_rgb555(0, 0, 0),
            checker_rgb555(255, 0, 0),
            checker_rgb555(0, 255, 0),
            checker_rgb555(0, 0, 255),
            checker_rgb555(255, 255, 0),
            checker_rgb555(255, 255, 255),
            checker_rgb555(255, 0, 255),
            checker_rgb555(0, 255, 255),
            checker_rgb555(0, 0, 0),
        };
        write_rgb555_palette(fbdd, colors, 9);
    }
    fclose(fbdd);
    free(pix);

    if (!bdd_load(bdd)) return 0;
    if (!bdb_load(bdb)) return 0;
    if (g_ni != PROOF_TILES || g_no != PROOF_TILES || g_n_pals != 1) {
        fprintf(stderr, "bgproof: reload count mismatch images=%d objects=%d palettes=%d\n",
                g_ni, g_no, g_n_pals);
        return 0;
    }
    if (g_img[0].idx != 1 || g_img[0].w != PROOF_TILE_W || g_img[0].h != PROOF_TILE_H ||
        g_img[PROOF_TILES - 1].idx != PROOF_TILES ||
        g_img[PROOF_TILES - 1].w != PROOF_TILE_W ||
        g_img[PROOF_TILES - 1].h != (PROOF_H - (PROOF_ROWS - 1) * PROOF_TILE_H) ||
        g_obj[0].wx != 0x4000 || g_obj[0].depth != 0 || g_obj[0].sy != 0 ||
        g_obj[0].ii != 1 || g_obj[0].fl != 0 ||
        g_obj[PROOF_TILES - 1].wx != 0x4000 ||
        g_obj[PROOF_TILES - 1].depth != (PROOF_COLS - 1) * PROOF_TILE_W ||
        g_obj[PROOF_TILES - 1].sy != (PROOF_ROWS - 1) * PROOF_TILE_H ||
        g_obj[PROOF_TILES - 1].ii != PROOF_TILES ||
        g_obj[PROOF_TILES - 1].fl != 0 ||
        strcmp(g_name, "BGPROF") != 0 ||
        strcmp(g_pal_name[0], "PROOFPAL") != 0) {
        fprintf(stderr, "bgproof: reload value mismatch\n");
        return 0;
    }

    fprintf(stderr, "bgproof: wrote and reloaded %s + %s\n", bdb, bdd);
    return 1;
}
