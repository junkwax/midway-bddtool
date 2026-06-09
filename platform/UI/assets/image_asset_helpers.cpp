#include "bg_editor_globals.h"
#include "undo_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
static bool text_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen && h[i]) {
            unsigned char a = (unsigned char)h[i];
            unsigned char b = (unsigned char)needle[i];
            if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 32);
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static bool image_matches_known_lod_source(const Img *im, const char *search)
{
    if (!im || !im->label[0] || !search || !search[0]) return false;
    const RuntimeExtraGuide *sets[] = {
        g_tower_runtime_guide_defaults,
        g_battle_runtime_guide_defaults
    };
    int counts[] = {
        g_tower_runtime_guide_defaults_count,
        g_battle_runtime_guide_defaults_count
    };
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < counts[s]; i++) {
            const RuntimeExtraGuide *g = &sets[s][i];
            if (strcasecmp(im->label, g->asset) == 0 && text_contains_ci(g->source, search))
                return true;
        }
    }
    return false;
}

static bool image_matches_search(const Img *im, const char *search, int search_idx)
{
    if (!im || !search || !search[0]) return true;
    if (search_idx >= 0 && im->idx == search_idx) return true;
    if (im->label[0] && text_contains_ci(im->label, search)) return true;
    if (im->source[0] && text_contains_ci(im->source, search)) return true;
    if (im->lod_ref && text_contains_ci("LOD", search)) return true;
    if (image_matches_known_lod_source(im, search)) return true;
    if (im->pal_idx >= 0 && im->pal_idx < g_n_pals &&
        text_contains_ci(g_pal_name[im->pal_idx], search))
        return true;
    return false;
}

bool image_is_imported_asset(const Img *im)
{
    return im && (im->source[0] || im->lod_ref);
}

bool image_passes_list_filter(const Img *im, int img_filter,
                              const char *img_search, int search_idx)
{
    if (!im) return false;
    if (img_filter == 1 && image_object_ref_count(im->idx) == 0) return false;
    if (img_filter == 2 && image_object_ref_count(im->idx) != 0) return false;
    if (img_filter == 3 && !im->lod_ref) return false;
    if (img_search && img_search[0] && !image_matches_search(im, img_search, search_idx))
        return false;
    return true;
}

int collect_matching_imported_images(int img_filter, const char *img_search,
                                     int search_idx, unsigned char *match,
                                     int *out_pixels, int *out_uses)
{
    if (out_pixels) *out_pixels = 0;
    if (out_uses) *out_uses = 0;
    int image_cap = editor_project_image_capacity();
    if (match && image_cap > 0)
        memset(match, 0, sizeof(match[0]) * (size_t)image_cap);
    int count = 0;
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (!image_is_imported_asset(im)) continue;
        if (!image_passes_list_filter(im, img_filter, img_search, search_idx)) continue;
        if (match) match[i] = true;
        count++;
        if (out_pixels) *out_pixels += im->w * im->h;
        if (out_uses) *out_uses += image_object_ref_count(im->idx);
    }
    return count;
}

int select_matching_imported_image_uses(int img_filter, const char *img_search, int search_idx)
{
    int image_cap = editor_project_image_capacity();
    int object_cap = editor_project_object_capacity();
    if (image_cap <= 0 || object_cap <= 0) return 0;
    std::vector<unsigned char> match_bytes((size_t)image_cap, 0);
    collect_matching_imported_images(img_filter, img_search, search_idx, match_bytes.data(), NULL, NULL);
    editor_project_clear_selection();
    int selected = 0;
    g_hl_obj = -1;
    for (int oi = 0; oi < g_no; oi++) {
        bool hit = false;
        for (int ii = 0; ii < g_ni; ii++) {
            if (!match_bytes[ii] || g_obj[oi].ii != g_img[ii].idx) continue;
            hit = true;
            break;
        }
        if (!hit) continue;
        g_sel_flags[oi] = 1;
        if (g_hl_obj < 0) g_hl_obj = oi;
        selected++;
    }
    return selected;
}

int delete_matching_imported_images_and_uses(int img_filter, const char *img_search, int search_idx)
{
    int image_cap = editor_project_image_capacity();
    int object_cap = editor_project_object_capacity();
    if (image_cap <= 0 || object_cap <= 0) return 0;
    std::vector<unsigned char> del_img((size_t)image_cap, 0);
    int delete_pixels = 0;
    int delete_uses = 0;
    int delete_images = collect_matching_imported_images(img_filter, img_search, search_idx,
                                                         del_img.data(), &delete_pixels, &delete_uses);
    if (delete_images <= 0) return 0;

    undo_save_ex("Delete Imported Source Group");

    int removed_objects = 0;
    for (int oi = g_no - 1; oi >= 0; oi--) {
        bool remove_obj = false;
        for (int ii = 0; ii < g_ni; ii++) {
            if (!del_img[ii] || g_obj[oi].ii != g_img[ii].idx) continue;
            remove_obj = true;
            break;
        }
        if (!remove_obj) continue;
        mk2_delete_object_preserve_order(oi);
        removed_objects++;
    }

    editor_project_delete_marked_images(del_img.data(), image_cap);

    if (g_place_tool_img >= g_ni) g_place_tool_img = g_ni - 1;
    if (g_tile_img >= g_ni) g_tile_img = g_ni - 1;
    if (g_last_import_img >= g_ni) g_last_import_img = g_ni - 1;
    if (g_block_edit_img >= g_ni) {
        g_block_edit_img = -1;
        g_block_edit_open = false;
    }
    editor_project_clear_selection();
    g_hl_obj = -1;
    sync_bdb_header_counts();
    g_need_rebuild = 1;
    g_dirty = 1;

    char msg[192];
    snprintf(msg, sizeof msg, "Deleted %d imported image(s), %d placement(s), freed %d px",
             delete_images, removed_objects, delete_pixels);
    stage_set_toast(msg);
    return delete_images;
}

int delete_unused_images_impl(bool imported_only, const char *undo_label)
{
    int delete_count = 0;
    int delete_pixels = 0;
    for (int i = 0; i < g_ni; i++) {
        if (image_object_ref_count(g_img[i].idx) != 0) continue;
        if (imported_only && !image_is_imported_asset(&g_img[i])) continue;
        delete_count++;
        delete_pixels += g_img[i].w * g_img[i].h;
    }
    if (delete_count <= 0) return 0;

    undo_save_ex(undo_label ? undo_label : "Delete Unused Images");
    {
        int image_cap = editor_project_image_capacity();
        std::vector<unsigned char> del_img((size_t)image_cap, 0);
        for (int i = 0; i < g_ni; i++) {
            bool del = image_object_ref_count(g_img[i].idx) == 0;
            if (del && imported_only && !image_is_imported_asset(&g_img[i]))
                del = false;
            if (del) del_img[(size_t)i] = 1;
        }
        editor_project_delete_marked_images(del_img.data(), image_cap);
    }
    if (g_place_tool_img >= g_ni) g_place_tool_img = g_ni - 1;
    if (g_last_import_img >= g_ni) g_last_import_img = g_ni - 1;
    g_need_rebuild = 1;
    g_dirty = 1;

    char msg[160];
    snprintf(msg, sizeof msg, "Deleted %d unused image(s), freed %d raw pixels",
             delete_count, delete_pixels);
    stage_set_toast(msg);
    return delete_count;
}
