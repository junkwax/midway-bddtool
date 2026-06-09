#include "Core/viewer_load.h"

#include "Core/app_diagnostics.h"
#include "Core/bdd_core.h"
#include "Core/bdd_metadata.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "Core/image_lookup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bdd_load(const char *path)
{
    BddCoreStage stage;
    int image_cap;
    int pal_cap;
    if (!editor_project_storage_init()) return 0;
    image_cap = editor_project_image_capacity();
    pal_cap = editor_project_palette_capacity();
    if (image_cap <= 0 || pal_cap <= 0) return 0;
    stage.init();
    if (!bdd_core_stage_load_bdd(&stage, path)) {
        fprintf(stderr, "bdd: %s\n", !stage.error.empty() ? stage.error.c_str() : "load failed");
        bdd_diag_write("ERROR: BDD file not found\n");
        stage.init();
        return 0;
    }
    BddCoreBdd *bdd = &stage.bdd;
    if (!editor_project_reserve_images((int)bdd->images.size()) ||
        !editor_project_reserve_palettes((int)bdd->palettes.size())) {
        fprintf(stderr, "bdd: could not reserve project storage for %d image(s), %d palette(s)\n",
                (int)bdd->images.size(), (int)bdd->palettes.size());
        stage.init();
        return 0;
    }
    image_cap = editor_project_image_capacity();
    pal_cap = editor_project_palette_capacity();
    (void)image_cap;
    (void)pal_cap;
    snprintf(g_bdd_path, sizeof g_bdd_path, "%s", path);

    editor_project_clear_images();
    editor_project_clear_palettes();
    for (int rec = 0; rec < (int)bdd->images.size(); rec++) {
        BddCoreImage *src = &bdd->images[rec];
        Img *dst = editor_project_append_image_slot();
        if (!dst) {
            fprintf(stderr, "bdd: image append failed\n");
            editor_project_clear_images();
            stage.init();
            return 0;
        }
        dst->idx = src->idx;
        dst->w = src->w;
        dst->h = src->h;
        dst->flags = src->flags;
        dst->pal_idx = -1;
        size_t pixel_count = src->pix.size();
        dst->pix = NULL;
        if (pixel_count > 0) {
            dst->pix = (Uint8 *)malloc(pixel_count);
            if (!dst->pix) {
                fprintf(stderr, "bdd: out of memory while copying image pixels\n");
                editor_project_clear_images();
                stage.init();
                return 0;
            }
            memcpy(dst->pix, src->pix.data(), pixel_count);
        }
    }

    for (int p = 0; p < (int)bdd->palettes.size(); p++) {
        BddCorePalette *src = &bdd->palettes[p];
        int pi = editor_project_append_palette_slot(src->name, src->count, src->argb);
        if (pi < 0) {
            fprintf(stderr, "bdd: palette append failed\n");
            break;
        }
        editor_project_set_palette_rgb555_cache(pi, src->rgb555, src->count);
        fprintf(stderr, "bdd: palette[%d] = %s (%d entries)\n", pi, src->name, src->count);
    }

    stage.init();
    if (g_ni == 0) {
        fprintf(stderr, "bdd: no images loaded from %s\n", path);
        return 0;
    }
    editor_project_load_bdd_metadata(path);
    fprintf(stderr, "bdd: loaded %d images from %s\n", g_ni, path);
    return 1;
}

int bdb_load(const char *path)
{
    BddCoreStage stage;
    if (!editor_project_storage_init()) return 0;
    stage.init();
    if (!bdd_core_stage_load_bdb(&stage, path)) {
        fprintf(stderr, "bdb: %s\n", !stage.error.empty() ? stage.error.c_str() : "load failed");
        bdd_diag_write("ERROR: BDB file not found\n");
        stage.init();
        return 0;
    }
    BddCoreBdb *bdb = &stage.bdb;
    if (!editor_project_reserve_modules((int)bdb->modules.size()) ||
        !editor_project_reserve_objects((int)bdb->objects.size())) {
        fprintf(stderr, "bdb: could not reserve project storage for %d module(s), %d object(s)\n",
                (int)bdb->modules.size(), (int)bdb->objects.size());
        stage.init();
        return 0;
    }

    snprintf(g_bdb_path, sizeof g_bdb_path, "%s", path);
    snprintf(g_bdb_header, sizeof g_bdb_header, "%s", bdb->header.c_str());
    if (bdb->name[0])
        snprintf(g_name, sizeof g_name, "%s", bdb->name);

    editor_project_clear_modules();
    editor_project_clear_objects();
    for (int m = 0; m < (int)bdb->modules.size(); m++) {
        editor_project_append_module_line(bdb->modules[m].line);
    }

    for (int i = 0; i < (int)bdb->objects.size(); i++) {
        const BddCoreObject *src = &bdb->objects[i];
        Obj *dst = editor_project_append_object_slot();
        if (!dst) break;
        dst->wx = src->wx;
        dst->depth = src->depth;
        dst->sy = src->sy;
        dst->ii = src->ii;
        dst->fl = src->fl;
        dst->hfl = (src->wx & 0x10) != 0;
        dst->vfl = (src->wx & 0x20) != 0;
        dst->order = src->order;
    }
    stage.init();

    editor_project_sort_objects_by_layer_order();

    BddCoreObject *palette_refs = (BddCoreObject *)malloc((size_t)g_no * sizeof *palette_refs);
    if (palette_refs) {
        for (int i = 0; i < g_no; i++) {
            palette_refs[i].wx = g_obj[i].wx;
            palette_refs[i].depth = g_obj[i].depth;
            palette_refs[i].sy = g_obj[i].sy;
            palette_refs[i].ii = g_obj[i].ii;
            palette_refs[i].fl = g_obj[i].fl;
            palette_refs[i].order = g_obj[i].order;
        }
        for (int i = 0; i < g_ni; i++) {
            if (g_img[i].pal_idx >= 0)
                continue;
            int pal = bdd_core_first_palette_for_image(palette_refs, g_no,
                                                       g_img[i].idx, 0, -1);
            if (pal >= 0)
                g_img[i].pal_idx = pal;
        }
        free(palette_refs);
    } else {
        for (int i = 0; i < g_no; i++) {
            Img *im = img_find(g_obj[i].ii);
            if (im && im->pal_idx < 0)
                im->pal_idx = g_obj[i].fl;
        }
    }

    fprintf(stderr, "bdb: loaded %d objects from %s\n", g_no, path);
    return g_no > 0;
}
