#include "Core/editor_project_globals.h"
#include "Core/image_lookup.h"
#include "UI/actions/export_viewport.h"
#include "UI/dialogs/native_file_dialogs.h"
#include "libs/stb_image_write.h"

#include <imgui.h>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

void export_viewport_png(void)
{
    if (!g_have_bdb || g_no == 0) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    int vw = (int)(ds.x / g_zoom), vh = (int)(ds.y / g_zoom);
    if (vw <= 0 || vh <= 0 || vw > 8192 || vh > 8192) return;

    unsigned char *buf = (unsigned char *)calloc((size_t)vw * vh, 4);
    if (!buf) return;

    for (int i = 0; i < g_no; i++) {
        Obj *o = &g_obj[i];
        Img *im = img_find(o->ii);
        if (!im || !im->pix) continue;
        const Uint32 *pal = (im->pal_idx >= 0 && im->pal_idx < g_n_pals) ? g_pals[im->pal_idx] : NULL;
        if (!pal) continue;
        int ox = o->depth - g_view_x, oy = o->sy - g_view_y;
        for (int y = 0; y < im->h; y++) {
            for (int x = 0; x < im->w; x++) {
                int sx = o->hfl ? (im->w - 1 - x) : x;
                int sy2 = (o->vfl ? (im->h - 1 - y) : y) * im->w;
                Uint8 v = im->pix[sy2 + sx];
                if (!v) continue;
                int px = ox + x, py = oy + y;
                if (px < 0 || px >= vw || py < 0 || py >= vh) continue;
                Uint32 c = pal[v];
                size_t off = ((size_t)py * vw + px) * 4;
                buf[off + 0] = (c >> 16) & 0xFF;
                buf[off + 1] = (c >> 8) & 0xFF;
                buf[off + 2] = c & 0xFF;
                buf[off + 3] = (c >> 24) & 0xFF;
            }
        }
    }

    char path[512] = "";
    if (file_dialog_save("Export Viewport",
            "PNG Files\0*.png\0All Files\0*.*\0", path, (int)sizeof path)) {
        size_t pl = strlen(path);
        if (pl < 4 || strcasecmp(path + pl - 4, ".png") != 0)
            strncat(path, ".png", sizeof path - pl - 1);
        stbi_write_png(path, vw, vh, 4, buf, vw * 4);
    }
    free(buf);
}
