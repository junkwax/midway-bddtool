#include "Core/image_lookup.h"

#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"

Img *img_find(int idx)
{
    if (!editor_project_storage_init()) return NULL;
    for (int i = 0; i < g_ni; i++)
        if (g_img[i].idx == idx) return &g_img[i];
    return NULL;
}

void img_free(void)
{
    editor_project_clear_images();
}
