#include "Core/bdd_metadata.h"
#include "bg_editor_globals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bdd_meta_path(const char *bdd_path, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s.meta", bdd_path ? bdd_path : "");
}

static void bdd_meta_clean_field(char *s)
{
    if (!s) return;
    for (char *p = s; *p; p++)
        if (*p == '\t' || *p == '\r' || *p == '\n')
            *p = ' ';
}

static int split_tabs(char *line, char **cols, int max_cols)
{
    int n = 0;
    if (!line || max_cols <= 0) return 0;
    cols[n++] = line;
    for (char *p = line; *p && n < max_cols; p++) {
        if (*p != '\t') continue;
        *p = '\0';
        cols[n++] = p + 1;
    }
    return n;
}

static Img *find_image_by_idx(int idx)
{
    for (int i = 0; i < g_ni; i++) {
        if (g_img[i].idx == idx)
            return &g_img[i];
    }
    return NULL;
}

void editor_project_save_bdd_metadata(const char *bdd_path)
{
    char meta[560];
    int any = 0;
    bdd_meta_path(bdd_path, meta, sizeof meta);
    if (!meta[0]) return;
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (im->label[0] || im->source[0] || im->anix || im->aniy ||
            im->anix2 || im->aniy2 || im->aniz2 || im->frm ||
            im->opals || im->pttblnum || im->lod_ref) {
            any = 1;
            break;
        }
    }
    if (!any) {
        remove(meta);
        return;
    }

    FILE *m = fopen(meta, "w");
    if (!m) return;
    fprintf(m, "# bddview image metadata v1\n");
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        char label[64], source[64];
        snprintf(label, sizeof label, "%s", im->label);
        snprintf(source, sizeof source, "%s", im->source);
        bdd_meta_clean_field(label);
        bdd_meta_clean_field(source);
        fprintf(m, "IMG\t%X\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                im->idx, label, source,
                im->anix, im->aniy, im->anix2, im->aniy2, im->aniz2,
                im->frm, im->opals, im->pttblnum, im->lod_ref);
    }
    fclose(m);
}

void editor_project_load_bdd_metadata(const char *bdd_path)
{
    char meta[560];
    bdd_meta_path(bdd_path, meta, sizeof meta);
    FILE *m = fopen(meta, "r");
    if (!m) return;

    char line[512];
    int applied = 0;
    while (fgets(line, sizeof line, m)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *cols[13] = {0};
        int n = split_tabs(line, cols, 13);
        if (n < 13 || strcmp(cols[0], "IMG") != 0) continue;
        int idx = (int)strtol(cols[1], NULL, 16);
        Img *im = find_image_by_idx(idx);
        if (!im) continue;
        snprintf(im->label, sizeof im->label, "%s", cols[2]);
        snprintf(im->source, sizeof im->source, "%s", cols[3]);
        im->anix = atoi(cols[4]);
        im->aniy = atoi(cols[5]);
        im->anix2 = atoi(cols[6]);
        im->aniy2 = atoi(cols[7]);
        im->aniz2 = atoi(cols[8]);
        im->frm = atoi(cols[9]);
        im->opals = atoi(cols[10]);
        im->pttblnum = atoi(cols[11]);
        im->lod_ref = atoi(cols[12]) ? 1 : 0;
        applied++;
    }
    fclose(m);
    if (applied > 0)
        fprintf(stderr, "bdd: loaded %d image metadata record(s) from %s\n", applied, meta);
}
