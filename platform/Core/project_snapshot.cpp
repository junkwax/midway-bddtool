#include "Core/project_snapshot.h"

#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"

#include <cstdlib>
#include <cstring>

int project_snapshot_clamp_count(int n, int cap)
{
    if (n < 0) return 0;
    if (n > cap) return cap;
    return n;
}

int project_snapshot_min_count(int a, int b)
{
    return (a < b) ? a : b;
}

void project_snapshot_free_pixels(ProjectSnapshot *snapshot)
{
    if (!snapshot || !snapshot->img) return;
    int ni = project_snapshot_clamp_count(snapshot->ni, snapshot->image_capacity);
    for (int i = 0; i < ni; i++) {
        free(snapshot->img[i].pix);
        snapshot->img[i].pix = nullptr;
    }
}

void project_snapshot_release(ProjectSnapshot *snapshot)
{
    if (!snapshot) return;
    project_snapshot_free_pixels(snapshot);
    free(snapshot->img);
    free(snapshot->obj);
    free(snapshot->obj_lock);
    free(snapshot->obj_hidden);
    free(snapshot->pals);
    free(snapshot->pal_count);
    free(snapshot->pal_name);
    free(snapshot->bdb_modules);
    std::memset(snapshot, 0, sizeof(*snapshot));
}

bool project_snapshot_ensure_storage(ProjectSnapshot *snapshot, bool include_object_flags)
{
    if (!snapshot) return false;
    int image_cap = editor_project_image_capacity();
    int object_cap = editor_project_object_capacity();
    int palette_cap = editor_project_palette_capacity();
    int module_cap = editor_project_module_capacity();
    if (image_cap <= 0 || object_cap <= 0 || palette_cap <= 0 || module_cap <= 0)
        return false;
    if (snapshot->img && snapshot->obj && snapshot->pals && snapshot->pal_count &&
        snapshot->pal_name && snapshot->bdb_modules &&
        (!include_object_flags || (snapshot->obj_lock && snapshot->obj_hidden)) &&
        snapshot->image_capacity == image_cap &&
        snapshot->object_capacity == object_cap &&
        snapshot->palette_capacity == palette_cap &&
        snapshot->module_capacity == module_cap)
        return true;

    project_snapshot_release(snapshot);
    snapshot->image_capacity = image_cap;
    snapshot->object_capacity = object_cap;
    snapshot->palette_capacity = palette_cap;
    snapshot->module_capacity = module_cap;
    snapshot->img = (Img *)calloc((size_t)image_cap, sizeof(Img));
    snapshot->obj = (Obj *)calloc((size_t)object_cap, sizeof(Obj));
    if (include_object_flags) {
        snapshot->obj_lock = (int *)calloc((size_t)object_cap, sizeof(int));
        snapshot->obj_hidden = (int *)calloc((size_t)object_cap, sizeof(int));
    }
    snapshot->pals = (Uint32 (*)[256])calloc((size_t)palette_cap, sizeof(snapshot->pals[0]));
    snapshot->pal_count = (int *)calloc((size_t)palette_cap, sizeof(int));
    snapshot->pal_name = (char (*)[64])calloc((size_t)palette_cap, sizeof(snapshot->pal_name[0]));
    snapshot->bdb_modules = (char (*)[256])calloc((size_t)module_cap, sizeof(snapshot->bdb_modules[0]));
    if (!snapshot->img || !snapshot->obj || !snapshot->pals || !snapshot->pal_count ||
        !snapshot->pal_name || !snapshot->bdb_modules ||
        (include_object_flags && (!snapshot->obj_lock || !snapshot->obj_hidden))) {
        project_snapshot_release(snapshot);
        return false;
    }
    return true;
}

bool project_snapshot_capture_current(ProjectSnapshot *snapshot, bool include_object_flags)
{
    if (!project_snapshot_ensure_storage(snapshot, include_object_flags)) return false;
    project_snapshot_free_pixels(snapshot);
    std::memset(snapshot->img, 0, sizeof(Img) * (size_t)snapshot->image_capacity);
    std::memset(snapshot->obj, 0, sizeof(Obj) * (size_t)snapshot->object_capacity);
    if (snapshot->obj_lock)
        std::memset(snapshot->obj_lock, 0, sizeof(int) * (size_t)snapshot->object_capacity);
    if (snapshot->obj_hidden)
        std::memset(snapshot->obj_hidden, 0, sizeof(int) * (size_t)snapshot->object_capacity);
    std::memset(snapshot->pals, 0, sizeof(snapshot->pals[0]) * (size_t)snapshot->palette_capacity);
    std::memset(snapshot->pal_count, 0, sizeof(snapshot->pal_count[0]) * (size_t)snapshot->palette_capacity);
    std::memset(snapshot->pal_name, 0, sizeof(snapshot->pal_name[0]) * (size_t)snapshot->palette_capacity);
    std::memset(snapshot->bdb_modules, 0, sizeof(snapshot->bdb_modules[0]) * (size_t)snapshot->module_capacity);

    snapshot->ni = project_snapshot_clamp_count(g_ni, snapshot->image_capacity);
    for (int i = 0; i < snapshot->ni; i++) {
        snapshot->img[i] = g_img[i];
        snapshot->img[i].pix = nullptr;
        if (g_img[i].pix && g_img[i].w > 0 && g_img[i].h > 0) {
            size_t sz = (size_t)g_img[i].w * (size_t)g_img[i].h;
            snapshot->img[i].pix = (Uint8 *)malloc(sz);
            if (snapshot->img[i].pix) std::memcpy(snapshot->img[i].pix, g_img[i].pix, sz);
        }
    }
    snapshot->no = project_snapshot_clamp_count(g_no, snapshot->object_capacity);
    std::memcpy(snapshot->obj, g_obj, sizeof(Obj) * (size_t)snapshot->no);
    if (include_object_flags && snapshot->obj_lock && snapshot->obj_hidden) {
        std::memcpy(snapshot->obj_lock, g_obj_lock, sizeof(int) * (size_t)snapshot->object_capacity);
        std::memcpy(snapshot->obj_hidden, g_obj_hidden, sizeof(int) * (size_t)snapshot->object_capacity);
    }
    snapshot->n_pals = project_snapshot_clamp_count(g_n_pals, snapshot->palette_capacity);
    std::memcpy(snapshot->pals, g_pals, (size_t)snapshot->palette_capacity * sizeof(g_pals[0]));
    std::memcpy(snapshot->pal_count, g_pal_count, (size_t)snapshot->palette_capacity * sizeof(g_pal_count[0]));
    std::memcpy(snapshot->pal_name, g_pal_name, (size_t)snapshot->palette_capacity * sizeof(g_pal_name[0]));
    std::memcpy(snapshot->bdb_header, g_bdb_header, sizeof(g_bdb_header));
    snapshot->bdb_num_modules = project_snapshot_clamp_count(g_bdb_num_modules, snapshot->module_capacity);
    std::memcpy(snapshot->bdb_modules, g_bdb_modules, (size_t)snapshot->module_capacity * sizeof(g_bdb_modules[0]));
    snapshot->have_bdb = g_have_bdb;
    std::memcpy(snapshot->name, g_name, sizeof(g_name));
    return true;
}

void project_snapshot_restore_current(const ProjectSnapshot *snapshot, bool include_object_flags)
{
    int image_cap = editor_project_image_capacity();
    int object_cap = editor_project_object_capacity();
    int palette_cap = editor_project_palette_capacity();
    int module_cap = editor_project_module_capacity();
    if (!snapshot || !snapshot->img || !snapshot->obj || !snapshot->pals ||
        image_cap <= 0 || object_cap <= 0 || palette_cap <= 0 || module_cap <= 0)
        return;

    editor_project_clear_images();
    int image_count = project_snapshot_clamp_count(
        snapshot->ni, project_snapshot_min_count(snapshot->image_capacity, image_cap));
    for (int i = 0; i < image_count; i++) {
        Img *dst = editor_project_append_image_slot();
        if (!dst) break;
        *dst = snapshot->img[i];
        dst->pix = nullptr;
        if (snapshot->img[i].pix && snapshot->img[i].w > 0 && snapshot->img[i].h > 0) {
            size_t sz = (size_t)snapshot->img[i].w * (size_t)snapshot->img[i].h;
            dst->pix = (Uint8 *)malloc(sz);
            if (dst->pix) std::memcpy(dst->pix, snapshot->img[i].pix, sz);
        }
    }
    editor_project_replace_objects(snapshot->obj, snapshot->no, snapshot->object_capacity,
                                   snapshot->obj_lock, snapshot->obj_hidden,
                                   include_object_flags ? 1 : 0);
    editor_project_replace_palettes(snapshot->pals, snapshot->pal_count, snapshot->pal_name,
                                    snapshot->n_pals, snapshot->palette_capacity);
    std::memcpy(g_bdb_header, snapshot->bdb_header, sizeof(g_bdb_header));
    editor_project_replace_modules(snapshot->bdb_modules, snapshot->bdb_num_modules,
                                   snapshot->module_capacity);
    g_have_bdb = snapshot->have_bdb;
    std::memcpy(g_name, snapshot->name, sizeof(g_name));
}
