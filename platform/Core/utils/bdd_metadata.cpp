#include "Core/bdd_metadata.h"

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

static BddImageMetadata *find_record_by_idx(BddImageMetadata *records, int record_count, int idx)
{
    if (!records || record_count <= 0) return NULL;
    for (int i = 0; i < record_count; i++) {
        if (records[i].idx == idx)
            return &records[i];
    }
    return NULL;
}

int bdd_metadata_save_records(const char *bdd_path,
                              const BddImageMetadata *records,
                              int record_count)
{
    char meta[560];
    int any = 0;
    bdd_meta_path(bdd_path, meta, sizeof meta);
    if (!meta[0]) return 0;
    for (int i = 0; i < record_count; i++) {
        const BddImageMetadata *rec = &records[i];
        if (rec->label[0] || rec->source[0] || rec->anix || rec->aniy ||
            rec->anix2 || rec->aniy2 || rec->aniz2 || rec->frm ||
            rec->opals || rec->pttblnum || rec->lod_ref) {
            any = 1;
            break;
        }
    }
    if (!any) {
        remove(meta);
        return 1;
    }

    FILE *m = fopen(meta, "w");
    if (!m) return 0;
    fprintf(m, "# bddview image metadata v1\n");
    for (int i = 0; i < record_count; i++) {
        const BddImageMetadata *rec = &records[i];
        char label[64], source[64];
        snprintf(label, sizeof label, "%s", rec->label);
        snprintf(source, sizeof source, "%s", rec->source);
        bdd_meta_clean_field(label);
        bdd_meta_clean_field(source);
        fprintf(m, "IMG\t%X\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                rec->idx, label, source,
                rec->anix, rec->aniy, rec->anix2, rec->aniy2, rec->aniz2,
                rec->frm, rec->opals, rec->pttblnum, rec->lod_ref);
    }
    fclose(m);
    return 1;
}

int bdd_metadata_load_records(const char *bdd_path,
                              BddImageMetadata *records,
                              int record_count)
{
    char meta[560];
    bdd_meta_path(bdd_path, meta, sizeof meta);
    FILE *m = fopen(meta, "r");
    if (!m) return 0;

    char line[512];
    int applied = 0;
    while (fgets(line, sizeof line, m)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *cols[13] = {0};
        int n = split_tabs(line, cols, 13);
        if (n < 13 || strcmp(cols[0], "IMG") != 0) continue;
        int idx = (int)strtol(cols[1], NULL, 16);
        BddImageMetadata *rec = find_record_by_idx(records, record_count, idx);
        if (!rec) continue;
        snprintf(rec->label, sizeof rec->label, "%s", cols[2]);
        snprintf(rec->source, sizeof rec->source, "%s", cols[3]);
        rec->anix = atoi(cols[4]);
        rec->aniy = atoi(cols[5]);
        rec->anix2 = atoi(cols[6]);
        rec->aniy2 = atoi(cols[7]);
        rec->aniz2 = atoi(cols[8]);
        rec->frm = atoi(cols[9]);
        rec->opals = atoi(cols[10]);
        rec->pttblnum = atoi(cols[11]);
        rec->lod_ref = atoi(cols[12]) ? 1 : 0;
        applied++;
    }
    fclose(m);
    if (applied > 0)
        fprintf(stderr, "bdd: loaded %d image metadata record(s) from %s\n", applied, meta);
    return applied;
}
