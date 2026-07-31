#include "UI/tools/palette_color_tools.h"

#include "bg_editor_globals.h"
#include "Core/bdd_core.h"
#include "UI/dialogs/native_file_dialogs.h"
#include "imgui.h"
#include "libs/stb_image.h"
#include "libs/stb_image_write.h"
#include "undo_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static Uint32 palette_tint_color(Uint32 src, const float tint[3], float strength,
                                 bool preserve_luminance)
{
    if ((src >> 24) == 0) return src;

    float sr = ((src >> 16) & 0xFF) / 255.0f;
    float sg = ((src >> 8) & 0xFF) / 255.0f;
    float sb = (src & 0xFF) / 255.0f;
    float tr = tint[0], tg = tint[1], tb = tint[2];

    if (preserve_luminance) {
        float source_luma = 0.2126f * sr + 0.7152f * sg + 0.0722f * sb;
        float tint_luma = 0.2126f * tr + 0.7152f * tg + 0.0722f * tb;
        if (tint_luma > 0.0001f) {
            float scale = source_luma / tint_luma;
            tr = clamp01(tr * scale);
            tg = clamp01(tg * scale);
            tb = clamp01(tb * scale);
        } else {
            tr = tg = tb = source_luma;
        }
    }

    float r = sr + (tr - sr) * strength;
    float g = sg + (tg - sg) * strength;
    float b = sb + (tb - sb) * strength;
    Uint32 a = src & 0xFF000000u;
    return a | ((Uint32)(clamp01(r) * 255.0f + 0.5f) << 16)
             | ((Uint32)(clamp01(g) * 255.0f + 0.5f) << 8)
             |  (Uint32)(clamp01(b) * 255.0f + 0.5f);
}

static void build_tinted_palette(int pal_i, const float tint[3], float strength,
                                 bool preserve_luminance, Uint32 out[256])
{
    memcpy(out, g_pals[pal_i], 256 * sizeof(Uint32));
    int count = g_pal_count[pal_i];
    if (count < 0) count = 0;
    if (count > 256) count = 256;
    for (int i = 1; i < count; i++)
        out[i] = palette_tint_color(out[i], tint, strength, preserve_luminance);
}

static void draw_tinted_palette_preview(int pal_i, const float tint[3], float strength,
                                        bool preserve_luminance)
{
    Uint32 preview[256];
    build_tinted_palette(pal_i, tint, strength, preserve_luminance, preview);
    int count = g_pal_count[pal_i];
    if (count > 32) count = 32;
    if (count < 0) count = 0;
    for (int i = 0; i < count; i++) {
        Uint32 c = preview[i];
        ImVec4 col(((c >> 16) & 0xFF) / 255.0f,
                   ((c >> 8) & 0xFF) / 255.0f,
                   (c & 0xFF) / 255.0f,
                   ((c >> 24) & 0xFF) / 255.0f);
        ImGui::PushID(i);
        if (i > 0) ImGui::SameLine(0.0f, 2.0f);
        ImGui::ColorButton("##tint_preview", col,
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           ImVec2(12.0f, 18.0f));
        ImGui::PopID();
    }
}

static bool palette_name_exists(const char *name)
{
    for (int pi = 0; pi < g_n_pals; pi++) {
        if (strcmp(g_pal_name[pi], name) == 0)
            return true;
    }
    return false;
}

static void palette_sequence_root(const char *source, char out[64])
{
    snprintf(out, 64, "%s", source && source[0] ? source : "PAL");
    char *underscore = strrchr(out, '_');
    if (!underscore || !underscore[1]) return;
    for (const char *p = underscore + 1; *p; p++)
        if (*p < '0' || *p > '9') return;
    char numbered_name[64];
    snprintf(numbered_name, sizeof numbered_name, "%s", out);
    *underscore = '\0';
    if (!out[0] || !palette_name_exists(out))
        snprintf(out, 64, "%s", numbered_name);
}

static void make_numbered_palette_name(const char *source, char out[64])
{
    char root[64];
    palette_sequence_root(source, root);
    root[48] = '\0';
    int sequence = 1;
    for (;;) {
        snprintf(out, 64, "%s_%d", root, sequence);
        if (!palette_name_exists(out)) return;
        sequence++;
    }
}

struct WandState {
    char path[1024] = "";
    char status[256] = "Choose a PNG, then click the object whose palette you want.";
    int width = 0;
    int height = 0;
    std::vector<Uint8> rgba;
    std::vector<Uint8> mask;
    std::vector<Uint32> colors;
    SDL_Texture *texture = NULL;
    int seed_x = -1;
    int seed_y = -1;
    int tolerance = 28;
    bool contiguous = true;
    bool texture_dirty = false;
};

static WandState s_wand;

struct ColorShiftPreviewState {
    bool active = false;
    int scope = 0;
    int document_id = 0;
    int selected_obj = -1;
    int selected_image = -1;
    int source_palette = -1;
    int original_palette_count = 0;
    int original_dirty = 0;
    bool original_sync_dirty = false;
    int original_selected_palette = 0;
    std::vector<Uint32> palettes;
    std::vector<int> palette_counts;
    std::vector<char> palette_names;
    std::vector<int> image_palettes;
    std::vector<int> object_palettes;
};

static ColorShiftPreviewState s_color_preview;

static int current_document_id(void)
{
    if (g_cur_doc >= 0 && g_cur_doc < g_num_docs)
        return g_docs[g_cur_doc].tab_id;
    return 0;
}

static void color_preview_release(void)
{
    s_color_preview.active = false;
    s_color_preview.palettes.clear();
    s_color_preview.palette_counts.clear();
    s_color_preview.palette_names.clear();
    s_color_preview.image_palettes.clear();
    s_color_preview.object_palettes.clear();
}

static bool color_preview_capture(int scope)
{
    color_preview_release();
    if (g_n_pals <= 0) return false;

    s_color_preview.scope = scope;
    s_color_preview.document_id = current_document_id();
    s_color_preview.original_palette_count = g_n_pals;
    s_color_preview.original_dirty = g_dirty;
    s_color_preview.original_sync_dirty = g_mk2_palette_sync_dirty;
    s_color_preview.original_selected_palette = g_sel_pal;
    s_color_preview.selected_obj = scope == 1 ? g_hl_obj : -1;
    if (scope == 1) {
        if (g_hl_obj < 0 || g_hl_obj >= g_no) return false;
        Img *im = img_find(g_obj[g_hl_obj].ii);
        s_color_preview.selected_image = g_obj[g_hl_obj].ii;
        s_color_preview.source_palette = object_palette_for_image(&g_obj[g_hl_obj], im);
        if (s_color_preview.source_palette < 0 ||
            s_color_preview.source_palette >= g_n_pals)
            return false;
    }

    s_color_preview.palettes.resize((size_t)g_n_pals * 256u);
    s_color_preview.palette_counts.resize((size_t)g_n_pals);
    s_color_preview.palette_names.resize((size_t)g_n_pals * 64u);
    memcpy(s_color_preview.palettes.data(), g_pals,
           (size_t)g_n_pals * 256u * sizeof(Uint32));
    memcpy(s_color_preview.palette_counts.data(), g_pal_count,
           (size_t)g_n_pals * sizeof(int));
    memcpy(s_color_preview.palette_names.data(), g_pal_name,
           (size_t)g_n_pals * 64u);
    s_color_preview.image_palettes.resize((size_t)g_ni);
    for (int ii = 0; ii < g_ni; ii++)
        s_color_preview.image_palettes[(size_t)ii] = g_img[ii].pal_idx;
    s_color_preview.object_palettes.resize((size_t)g_no);
    for (int oi = 0; oi < g_no; oi++)
        s_color_preview.object_palettes[(size_t)oi] = g_obj[oi].fl;
    s_color_preview.active = true;
    return true;
}

static bool color_preview_same_document(void)
{
    return s_color_preview.active &&
           s_color_preview.document_id == current_document_id();
}

static void color_preview_restore_original(void)
{
    if (!color_preview_same_document()) return;
    const Uint32 (*palettes)[256] =
        reinterpret_cast<const Uint32 (*)[256]>(s_color_preview.palettes.data());
    const char (*names)[64] =
        reinterpret_cast<const char (*)[64]>(s_color_preview.palette_names.data());
    editor_project_replace_palettes(palettes,
                                    s_color_preview.palette_counts.data(), names,
                                    s_color_preview.original_palette_count,
                                    s_color_preview.original_palette_count);
    int image_count = (int)s_color_preview.image_palettes.size();
    if (image_count > g_ni) image_count = g_ni;
    for (int ii = 0; ii < image_count; ii++)
        g_img[ii].pal_idx = s_color_preview.image_palettes[(size_t)ii];
    int object_count = (int)s_color_preview.object_palettes.size();
    if (object_count > g_no) object_count = g_no;
    for (int oi = 0; oi < object_count; oi++)
        g_obj[oi].fl = s_color_preview.object_palettes[(size_t)oi];
    g_sel_pal = s_color_preview.original_selected_palette;
    g_dirty = s_color_preview.original_dirty;
    g_mk2_palette_sync_dirty = s_color_preview.original_sync_dirty;
    g_need_rebuild = 1;
}

static int apply_object_color_shift(int selected_obj, int selected_image,
                                    int source_pal, const float tint[3],
                                    float strength, bool preserve_luminance,
                                    int *out_target_pal)
{
    if (selected_obj < 0 || selected_obj >= g_no ||
        source_pal < 0 || source_pal >= g_n_pals)
        return -1;
    Uint32 next[256];
    build_tinted_palette(source_pal, tint, strength, preserve_luminance, next);
    if (!editor_project_reserve_palettes(g_n_pals + 1)) return -1;
    char name[64];
    make_numbered_palette_name(g_pal_name[source_pal], name);
    int target_pal = editor_project_append_palette_slot(name,
                                                         g_pal_count[source_pal], next);
    if (target_pal < 0) return -1;

    int changed = 0;
    for (int oi = 0; oi < g_no; oi++) {
        if (g_obj[oi].ii != selected_image) continue;
        Img *candidate_image = img_find(g_obj[oi].ii);
        if (object_palette_for_image(&g_obj[oi], candidate_image) != source_pal)
            continue;
        g_obj[oi].fl = target_pal;
        changed++;
    }
    if (out_target_pal) *out_target_pal = target_pal;
    return changed;
}

static int apply_global_color_shift(const float tint[3], float strength,
                                    bool preserve_luminance,
                                    int original_selected_palette,
                                    int *out_selected_palette)
{
    int source_count = g_n_pals;
    if (source_count <= 0 ||
        !editor_project_reserve_palettes(source_count + source_count))
        return -1;

    std::vector<int> remap((size_t)source_count, -1);
    for (int pi = 0; pi < source_count; pi++) {
        Uint32 next[256];
        build_tinted_palette(pi, tint, strength, preserve_luminance, next);
        char name[64];
        make_numbered_palette_name(g_pal_name[pi], name);
        int target = editor_project_append_palette_slot(name, g_pal_count[pi], next);
        if (target < 0) return -1;
        remap[(size_t)pi] = target;
    }

    for (int ii = 0; ii < g_ni; ii++) {
        int pal = g_img[ii].pal_idx;
        if (pal >= 0 && pal < source_count)
            g_img[ii].pal_idx = remap[(size_t)pal];
    }
    for (int oi = 0; oi < g_no; oi++) {
        int pal = g_obj[oi].fl;
        if (pal >= 0 && pal < source_count)
            g_obj[oi].fl = remap[(size_t)pal];
    }
    if (out_selected_palette) {
        *out_selected_palette =
            original_selected_palette >= 0 && original_selected_palette < source_count
            ? remap[(size_t)original_selected_palette]
            : original_selected_palette;
    }
    return source_count;
}

static bool color_preview_apply(const float tint[3], float strength,
                                bool preserve_luminance)
{
    if (!color_preview_same_document()) return false;
    color_preview_restore_original();
    if (s_color_preview.scope == 0) {
        int selected_palette = s_color_preview.original_selected_palette;
        if (apply_global_color_shift(tint, strength, preserve_luminance,
                                     s_color_preview.original_selected_palette,
                                     &selected_palette) < 0)
            return false;
        g_sel_pal = selected_palette;
    } else {
        if (apply_object_color_shift(s_color_preview.selected_obj,
                                     s_color_preview.selected_image,
                                     s_color_preview.source_palette,
                                     tint, strength, preserve_luminance,
                                     NULL) < 0)
            return false;
    }
    /* Preview state is intentionally not a project edit. */
    g_dirty = s_color_preview.original_dirty;
    g_mk2_palette_sync_dirty = s_color_preview.original_sync_dirty;
    g_need_rebuild = 1;
    return true;
}

static void wand_destroy_texture(void)
{
    if (s_wand.texture) {
        SDL_DestroyTexture(s_wand.texture);
        s_wand.texture = NULL;
    }
}

static bool wand_pixel_matches(int pixel_index, int seed_index, int tolerance)
{
    const Uint8 *p = &s_wand.rgba[(size_t)pixel_index * 4u];
    const Uint8 *seed = &s_wand.rgba[(size_t)seed_index * 4u];
    if (p[3] < 8) return false;
    int dr = (int)p[0] - (int)seed[0];
    int dg = (int)p[1] - (int)seed[1];
    int db = (int)p[2] - (int)seed[2];
    int da = (int)p[3] - (int)seed[3];
    int limit = tolerance * tolerance * 3;
    return dr * dr + dg * dg + db * db <= limit && da * da <= tolerance * tolerance;
}

static void wand_collect_palette(void)
{
    std::unordered_map<Uint16, int> frequency;
    int selected_pixels = 0;
    for (size_t i = 0; i < s_wand.mask.size(); i++) {
        if (!s_wand.mask[i]) continue;
        const Uint8 *p = &s_wand.rgba[i * 4u];
        if (p[3] < 8) continue;
        Uint32 argb = 0xFF000000u | ((Uint32)p[0] << 16) |
                      ((Uint32)p[1] << 8) | (Uint32)p[2];
        frequency[bdd_core_argb_to_rgb555(argb)]++;
        selected_pixels++;
    }

    std::vector<std::pair<Uint16, int>> ranked;
    ranked.reserve(frequency.size());
    for (const auto &item : frequency)
        ranked.push_back(item);
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    bool capped = ranked.size() > 255u;
    if (ranked.size() > 255u) ranked.resize(255u);
    s_wand.colors.clear();
    s_wand.colors.reserve(ranked.size() + 1u);
    s_wand.colors.push_back(0u);
    for (const auto &item : ranked)
        s_wand.colors.push_back(bdd_core_rgb555_to_argb(item.first));

    snprintf(s_wand.status, sizeof s_wand.status,
             "%d pixel(s) selected; %d RGB555 color(s)%s.",
             selected_pixels, (int)s_wand.colors.size() - 1,
             capped ? " (most-used 255 kept)" : "");
}

static void wand_rebuild_texture(void)
{
    wand_destroy_texture();
    s_wand.texture_dirty = false;
    if (!g_rend || s_wand.width <= 0 || s_wand.height <= 0 || s_wand.rgba.empty())
        return;
    size_t pixels = (size_t)s_wand.width * (size_t)s_wand.height;
    std::vector<Uint32> preview(pixels, 0u);
    bool have_selection = !s_wand.colors.empty();
    for (int y = 0; y < s_wand.height; y++) {
        for (int x = 0; x < s_wand.width; x++) {
            size_t i = (size_t)y * (size_t)s_wand.width + (size_t)x;
            const Uint8 *p = &s_wand.rgba[i * 4u];
            Uint8 r = p[0], g = p[1], b = p[2], a = p[3];
            if (have_selection && !s_wand.mask[i]) {
                r = (Uint8)(r * 0.28f);
                g = (Uint8)(g * 0.28f);
                b = (Uint8)(b * 0.28f);
                a = (Uint8)(a * 0.72f);
            } else if (have_selection && s_wand.mask[i]) {
                bool edge = x == 0 || y == 0 || x + 1 == s_wand.width || y + 1 == s_wand.height;
                if (!edge) {
                    edge = !s_wand.mask[i - 1] || !s_wand.mask[i + 1] ||
                           !s_wand.mask[i - (size_t)s_wand.width] ||
                           !s_wand.mask[i + (size_t)s_wand.width];
                }
                if (edge) {
                    r = (Uint8)((r + 30) / 2);
                    g = (Uint8)((g + 235) / 2);
                    b = (Uint8)((b + 255) / 2);
                    a = 255;
                }
            }
            preview[i] = ((Uint32)a << 24) | ((Uint32)r << 16) |
                         ((Uint32)g << 8) | (Uint32)b;
        }
    }
    s_wand.texture = SDL_CreateTexture(g_rend, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC,
                                       s_wand.width, s_wand.height);
    if (!s_wand.texture) return;
    SDL_SetTextureBlendMode(s_wand.texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(s_wand.texture, NULL, preview.data(),
                      s_wand.width * (int)sizeof(Uint32));
}

static void wand_select(void)
{
    const int w = s_wand.width;
    const int h = s_wand.height;
    if (w <= 0 || h <= 0 || s_wand.seed_x < 0 || s_wand.seed_y < 0 ||
        s_wand.seed_x >= w || s_wand.seed_y >= h)
        return;
    int seed = s_wand.seed_y * w + s_wand.seed_x;
    if (s_wand.rgba[(size_t)seed * 4u + 3u] < 8) {
        s_wand.mask.assign((size_t)w * (size_t)h, 0);
        s_wand.colors.clear();
        snprintf(s_wand.status, sizeof s_wand.status,
                 "That pixel is transparent. Click a visible part of the object.");
        s_wand.texture_dirty = true;
        return;
    }

    s_wand.mask.assign((size_t)w * (size_t)h, 0);
    if (!s_wand.contiguous) {
        for (int i = 0; i < w * h; i++)
            s_wand.mask[(size_t)i] = wand_pixel_matches(i, seed, s_wand.tolerance) ? 1 : 0;
    } else {
        std::queue<int> pending;
        s_wand.mask[(size_t)seed] = 1;
        pending.push(seed);
        while (!pending.empty()) {
            int i = pending.front();
            pending.pop();
            int x = i % w;
            int y = i / w;
            const int neighbors[4] = {i - 1, i + 1, i - w, i + w};
            const bool valid[4] = {x > 0, x + 1 < w, y > 0, y + 1 < h};
            for (int n = 0; n < 4; n++) {
                int next = neighbors[n];
                if (!valid[n] || s_wand.mask[(size_t)next]) continue;
                if (!wand_pixel_matches(next, seed, s_wand.tolerance)) continue;
                s_wand.mask[(size_t)next] = 1;
                pending.push(next);
            }
        }
    }
    wand_collect_palette();
    s_wand.texture_dirty = true;
}

static bool wand_load_png(const char *path)
{
    int w = 0, h = 0, channels = 0;
    unsigned char *loaded = stbi_load(path, &w, &h, &channels, 4);
    if (!loaded || w <= 0 || h <= 0 ||
        (size_t)w * (size_t)h > 16777216u) {
        if (loaded) stbi_image_free(loaded);
        snprintf(s_wand.status, sizeof s_wand.status,
                 "Could not load that PNG, or it is larger than 16 megapixels.");
        return false;
    }
    wand_destroy_texture();
    s_wand.width = w;
    s_wand.height = h;
    s_wand.rgba.assign(loaded, loaded + (size_t)w * (size_t)h * 4u);
    stbi_image_free(loaded);
    s_wand.mask.assign((size_t)w * (size_t)h, 0);
    s_wand.colors.clear();
    s_wand.seed_x = s_wand.seed_y = -1;
    snprintf(s_wand.path, sizeof s_wand.path, "%s", path);
    snprintf(s_wand.status, sizeof s_wand.status,
             "Loaded %dx%d PNG. Click a visible part of the object.", w, h);
    wand_rebuild_texture();
    return true;
}

static void draw_extracted_swatches(void)
{
    if (s_wand.colors.size() <= 1u) return;
    float width = ImGui::GetContentRegionAvail().x;
    int columns = (int)(width / 20.0f);
    if (columns < 1) columns = 1;
    for (size_t i = 1; i < s_wand.colors.size(); i++) {
        Uint32 c = s_wand.colors[i];
        ImVec4 color(((c >> 16) & 0xFF) / 255.0f,
                     ((c >> 8) & 0xFF) / 255.0f,
                     (c & 0xFF) / 255.0f, 1.0f);
        ImGui::PushID((int)i);
        if (((int)i - 1) % columns != 0) ImGui::SameLine(0.0f, 2.0f);
        ImGui::ColorButton("##wand_swatch", color,
                           ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoTooltip,
                           ImVec2(16.0f, 16.0f));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("#%02X%02X%02X", (c >> 16) & 0xFF,
                              (c >> 8) & 0xFF, c & 0xFF);
        ImGui::PopID();
    }
}

static bool path_has_extension(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *base = slash;
    if (!base || (backslash && backslash > base)) base = backslash;
    const char *dot = strrchr(path, '.');
    return dot && (!base || dot > base);
}

static bool write_palette_swatch_png(const char *path, const Uint32 *colors, int count)
{
    if (!path || !path[0] || !colors) return false;
    if (count < 1) count = 1;
    if (count > 256) count = 256;
    const int cols = 16;
    const int cell = 20;
    int rows = (count + cols - 1) / cols;
    int w = cols * cell;
    int h = rows * cell;
    /* Keep the unused sheet and index-zero cell transparent so importing this
       swatch later introduces only the colors that were actually extracted. */
    std::vector<Uint8> pixels((size_t)w * (size_t)h * 4u, 0);
    for (int i = 0; i < count; i++) {
        Uint32 color = colors[i];
        int cx = (i % cols) * cell;
        int cy = (i / cols) * cell;
        for (int y = 0; y < cell; y++) {
            for (int x = 0; x < cell; x++) {
                Uint32 c = i == 0 ? 0u : color;
                size_t off = ((size_t)(cy + y) * (size_t)w + (size_t)(cx + x)) * 4u;
                pixels[off] = (Uint8)((c >> 16) & 0xFF);
                pixels[off + 1] = (Uint8)((c >> 8) & 0xFF);
                pixels[off + 2] = (Uint8)(c & 0xFF);
                pixels[off + 3] = (Uint8)((c >> 24) & 0xFF);
            }
        }
    }
    return stbi_write_png(path, w, h, 4, pixels.data(), w * 4) != 0;
}

static bool wand_export_swatch_png(const char *path)
{
    return write_palette_swatch_png(path, s_wand.colors.data(),
                                    (int)s_wand.colors.size());
}

static void sanitize_palette_filename(const char *name, char out[64])
{
    snprintf(out, 64, "%s", name && name[0] ? name : "PAL");
    for (char *p = out; *p; p++) {
        char c = *p;
        bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!valid) *p = '_';
    }
}

} // namespace

void draw_palette_color_shift_tool(void)
{
    static float tint[3] = {0.28f, 0.58f, 1.0f};
    static int strength_percent = 65;
    static bool preserve_luminance = true;
    static int scope = 0; /* 0 = global, 1 = selected object and duplicates */

    if (s_color_preview.active && !color_preview_same_document())
        color_preview_release();
    if (s_color_preview.active && s_color_preview.scope == 1 &&
        g_hl_obj != s_color_preview.selected_obj) {
        palette_color_shift_cancel_preview();
        stage_set_toast("Palette preview canceled because the selected object changed");
    }

    ImGui::TextWrapped("Recolor palette hues while retaining indexed image data. Changes appear live in the current stage view, then save as numbered copies so the originals remain untouched.");
    bool controls_changed = ImGui::RadioButton("Global", &scope, 0);
    ImGui::SameLine();
    controls_changed |= ImGui::RadioButton("Selected object + duplicates", &scope, 1);
    if (s_color_preview.active && scope != s_color_preview.scope)
        palette_color_shift_cancel_preview();
    if (scope == 1) {
        if (g_hl_obj >= 0 && g_hl_obj < g_no)
            ImGui::TextDisabled("Matches image 0x%02X and its current palette.", g_obj[g_hl_obj].ii);
        else
            ImGui::TextDisabled("Select an object in the stage first.");
    }

    controls_changed |= ImGui::ColorPicker3("Color wheel", tint,
                                             ImGuiColorEditFlags_PickerHueWheel |
                                             ImGuiColorEditFlags_DisplayRGB |
                                             ImGuiColorEditFlags_NoSidePreview |
                                             ImGuiColorEditFlags_NoSmallPreview);
    controls_changed |= ImGui::SliderInt("Strength", &strength_percent, 0, 100, "%d%%");
    controls_changed |= ImGui::Checkbox("Preserve original brightness", &preserve_luminance);
    if (!s_color_preview.active && g_sel_pal >= 0 && g_sel_pal < g_n_pals) {
        ImGui::TextDisabled("Selected palette preview");
        draw_tinted_palette_preview(g_sel_pal, tint, strength_percent / 100.0f,
                                    preserve_luminance);
    } else if (s_color_preview.active) {
        ImGui::TextDisabled("The current stage view is showing this preview.");
    }

    bool disabled = g_n_pals <= 0 ||
                    (scope == 1 && (g_hl_obj < 0 || g_hl_obj >= g_no));
    if (disabled) ImGui::BeginDisabled();
    bool preview_requested = false;
    if (!s_color_preview.active)
        preview_requested = ImGui::Button("Preview in Current View", ImVec2(-1.0f, 0.0f));
    if (disabled) ImGui::EndDisabled();

    if ((controls_changed || preview_requested) && !disabled) {
        if (!s_color_preview.active && !color_preview_capture(scope)) {
            stage_set_toast("Could not start palette preview");
        } else if (!color_preview_apply(tint, strength_percent / 100.0f,
                                        preserve_luminance)) {
            palette_color_shift_cancel_preview();
            stage_set_toast("Could not update palette preview");
        }
    }

    if (s_color_preview.active) {
        ImGui::TextColored(ImVec4(0.35f, 0.9f, 1.0f, 1.0f),
                           "Live preview - the project is not changed yet.");
        if (ImGui::Button("Accept Preview", ImVec2(-1.0f, 0.0f))) {
            int accepted_scope = s_color_preview.scope;
            int selected_obj = s_color_preview.selected_obj;
            int selected_image = s_color_preview.selected_image;
            int source_pal = s_color_preview.source_palette;
            int original_selected_palette = s_color_preview.original_selected_palette;
            color_preview_restore_original();
            color_preview_release();
            int required_palettes = g_n_pals + (accepted_scope == 0 ? g_n_pals : 1);
            if (!editor_project_reserve_palettes(required_palettes)) {
                stage_set_toast("Not enough palette slots to preserve the originals");
                return;
            }
            undo_save_ex(accepted_scope == 0 ? "Global Palette Copy Shift" :
                                               "Object Palette Copy Shift");
            int target_pal = -1;
            int changed = 0;
            if (accepted_scope == 0) {
                changed = apply_global_color_shift(tint,
                                                   strength_percent / 100.0f,
                                                   preserve_luminance,
                                                   original_selected_palette,
                                                   &target_pal);
                if (target_pal >= 0) g_sel_pal = target_pal;
            } else {
                changed = apply_object_color_shift(selected_obj, selected_image,
                                                   source_pal, tint,
                                                   strength_percent / 100.0f,
                                                   preserve_luminance, &target_pal);
                if (target_pal >= 0) g_sel_pal = target_pal;
            }
            g_dirty = 1;
            g_need_rebuild = 1;
            g_mk2_palette_sync_dirty = true;
            if (accepted_scope == 0) {
                char message[128];
                snprintf(message, sizeof message,
                         "Created %d numbered palette copy/copies; originals preserved",
                         changed > 0 ? changed : 0);
                stage_set_toast(message);
            } else {
                char message[128];
                snprintf(message, sizeof message,
                         "Accepted object recolor for %d matching placement(s)",
                         changed > 0 ? changed : 0);
                stage_set_toast(message);
            }
        }
        if (s_color_preview.active) {
            if (ImGui::Button("Cancel Preview", ImVec2(-1.0f, 0.0f))) {
                palette_color_shift_cancel_preview();
                stage_set_toast("Palette preview canceled");
            }
        }
    }
}

void draw_palette_magic_wand_tool(void)
{
    ImGui::TextWrapped("Load a PNG and click a visible region to extract only that region's hardware palette.");
    if (ImGui::Button("Load PNG...")) {
        char path[1024];
        if (file_dialog_open("Extract Palette from PNG",
                             "PNG Files\0*.PNG;*.png\0All Files\0*.*\0",
                             path, sizeof path))
            wand_load_png(path);
    }
    if (s_wand.path[0]) {
        ImGui::SameLine();
        const char *base = s_wand.path;
        for (const char *p = s_wand.path; *p; p++)
            if (*p == '/' || *p == '\\') base = p + 1;
        ImGui::TextDisabled("%s", base);
    }

    if (!s_wand.rgba.empty()) {
        bool changed = ImGui::SliderInt("Tolerance", &s_wand.tolerance, 0, 255);
        changed |= ImGui::Checkbox("Connected pixels only", &s_wand.contiguous);
        if (changed && s_wand.seed_x >= 0)
            wand_select();

        /* A click happens after ImGui has queued the current texture for this
           frame. Rebuild here on the following frame, before queueing it again. */
        if (s_wand.texture_dirty)
            wand_rebuild_texture();

        float avail = ImGui::GetContentRegionAvail().x;
        float scale = avail / (float)s_wand.width;
        float height_scale = 360.0f / (float)s_wand.height;
        if (scale > height_scale) scale = height_scale;
        if (scale > 4.0f) scale = 4.0f;
        if (scale < 0.02f) scale = 0.02f;
        ImVec2 shown((float)s_wand.width * scale, (float)s_wand.height * scale);
        if (s_wand.texture) {
            ImGui::Image(s_wand.texture, shown);
            ImVec2 min = ImGui::GetItemRectMin();
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 mouse = ImGui::GetIO().MousePos;
                int x = (int)((mouse.x - min.x) / shown.x * s_wand.width);
                int y = (int)((mouse.y - min.y) / shown.y * s_wand.height);
                if (x < 0) x = 0;
                if (y < 0) y = 0;
                if (x >= s_wand.width) x = s_wand.width - 1;
                if (y >= s_wand.height) y = s_wand.height - 1;
                s_wand.seed_x = x;
                s_wand.seed_y = y;
                wand_select();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to select a palette region");
        }
    }

    ImGui::TextWrapped("%s", s_wand.status);
    draw_extracted_swatches();
    bool no_palette = s_wand.colors.size() <= 1u;
    if (no_palette) ImGui::BeginDisabled();
    if (ImGui::Button("Add to Project Palettes")) {
        Uint32 colors[256] = {};
        int count = (int)s_wand.colors.size();
        memcpy(colors, s_wand.colors.data(), (size_t)count * sizeof(Uint32));
        undo_save_ex("Add Extracted PNG Palette");
        char name[64] = "PNG_SELECTION";
        if (s_wand.path[0]) {
            const char *base = s_wand.path;
            for (const char *p = s_wand.path; *p; p++)
                if (*p == '/' || *p == '\\') base = p + 1;
            snprintf(name, sizeof name, "%.48s", base);
            char *dot = strrchr(name, '.');
            if (dot) *dot = '\0';
        }
        int pi = editor_project_append_palette_slot(name, count, colors);
        if (pi >= 0) {
            g_sel_pal = pi;
            g_dirty = 1;
            g_need_rebuild = 1;
            g_mk2_palette_sync_dirty = true;
            stage_set_toast("Added extracted PNG palette to project");
        } else {
            stage_set_toast("Could not add extracted palette");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Swatch PNG...")) {
        char path[1024];
        if (file_dialog_save("Export Extracted Palette",
                             "PNG Files\0*.png\0All Files\0*.*\0",
                             path, sizeof path)) {
            char final_path[1024];
            snprintf(final_path, sizeof final_path, "%s%s", path,
                     path_has_extension(path) ? "" : ".png");
            if (wand_export_swatch_png(final_path))
                stage_set_toast("Exported extracted palette swatch");
            else
                stage_set_toast("Could not export palette swatch");
        }
    }
    if (no_palette) ImGui::EndDisabled();
}

void palette_color_tools_shutdown(void)
{
    palette_color_shift_cancel_preview();
    wand_destroy_texture();
    s_wand.rgba.clear();
    s_wand.mask.clear();
    s_wand.colors.clear();
}

void export_all_palettes_to_folder_dialog(void)
{
    if (g_n_pals <= 0) {
        stage_set_toast("No palettes to export");
        return;
    }
    char folder[1024];
    if (!folder_dialog_open("Export All Palettes", folder, sizeof folder))
        return;

    int exported = 0;
    int failed = 0;
    for (int pi = 0; pi < g_n_pals; pi++) {
        char safe_name[64];
        char filename[96];
        char path[1200];
        sanitize_palette_filename(g_pal_name[pi], safe_name);
        snprintf(filename, sizeof filename, "%03d_%s.png", pi, safe_name);
        path_join(path, sizeof path, folder, filename);
        if (write_palette_swatch_png(path, g_pals[pi], g_pal_count[pi]))
            exported++;
        else
            failed++;
    }
    char message[160];
    snprintf(message, sizeof message,
             failed ? "Exported %d palette(s); %d failed" :
                      "Exported all %d palette(s)",
             exported, failed);
    stage_set_toast(message);
}

bool palette_color_shift_preview_active(void)
{
    return s_color_preview.active;
}

void palette_color_shift_cancel_preview(void)
{
    if (!s_color_preview.active) return;
    color_preview_restore_original();
    color_preview_release();
}
