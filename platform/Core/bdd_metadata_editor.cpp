#include "Core/bdd_metadata.h"
#include "Core/editor_project_globals.h"

#include <cstdio>
#include <vector>

static void metadata_from_image(BddImageMetadata *rec, const Img *im)
{
    if (!rec || !im) return;
    rec->idx = im->idx;
    rec->anix = im->anix;
    rec->aniy = im->aniy;
    rec->anix2 = im->anix2;
    rec->aniy2 = im->aniy2;
    rec->aniz2 = im->aniz2;
    rec->frm = im->frm;
    rec->opals = im->opals;
    rec->pttblnum = im->pttblnum;
    rec->lod_ref = im->lod_ref;
    std::snprintf(rec->label, sizeof rec->label, "%s", im->label);
    std::snprintf(rec->source, sizeof rec->source, "%s", im->source);
}

static void metadata_to_image(Img *im, const BddImageMetadata *rec)
{
    if (!im || !rec) return;
    std::snprintf(im->label, sizeof im->label, "%s", rec->label);
    std::snprintf(im->source, sizeof im->source, "%s", rec->source);
    im->anix = rec->anix;
    im->aniy = rec->aniy;
    im->anix2 = rec->anix2;
    im->aniy2 = rec->aniy2;
    im->aniz2 = rec->aniz2;
    im->frm = rec->frm;
    im->opals = rec->opals;
    im->pttblnum = rec->pttblnum;
    im->lod_ref = rec->lod_ref ? 1 : 0;
}

void editor_project_save_bdd_metadata(const char *bdd_path)
{
    std::vector<BddImageMetadata> records((size_t)((g_ni > 0) ? g_ni : 0));
    for (int i = 0; i < g_ni; i++) {
        metadata_from_image(&records[(size_t)i], &g_img[i]);
    }
    bdd_metadata_save_records(bdd_path,
                              records.empty() ? nullptr : records.data(),
                              (int)records.size());
}

void editor_project_load_bdd_metadata(const char *bdd_path)
{
    std::vector<BddImageMetadata> records((size_t)((g_ni > 0) ? g_ni : 0));
    for (int i = 0; i < g_ni; i++) {
        metadata_from_image(&records[(size_t)i], &g_img[i]);
    }
    int applied = bdd_metadata_load_records(bdd_path,
                                            records.empty() ? nullptr : records.data(),
                                            (int)records.size());
    if (applied <= 0) return;
    for (int i = 0; i < g_ni; i++) {
        metadata_to_image(&g_img[i], &records[(size_t)i]);
    }
}
