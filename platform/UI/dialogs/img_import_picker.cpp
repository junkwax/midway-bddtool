#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/img_format.h"
#include "imgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

struct ImgImportChoice {
    int rec_index;
    int w, h;
    int palnum;
    int anix, aniy;
    int flags;
    bool preview_tried;
    SDL_Texture *preview_tex;
    bool valid;
    bool selected;
    char label[64];
};

static bool g_img_import_picker_open = false;
static bool g_img_import_picker_request = false;
static char g_img_import_picker_path[512] = "";
static char g_img_import_picker_status[256] = "";
static char g_img_import_picker_filter[64] = "";
static std::vector<ImgImportChoice> g_img_import_choices;

static const char *img_picker_basename_ptr(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *sep = slash;
    if (!sep || (backslash && backslash > sep)) sep = backslash;
    return sep ? sep + 1 : path;
}

static bool img_picker_text_contains_ci(const char *haystack, const char *needle)
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

static void img_import_discard_preview_textures(void)
{
    for (size_t i = 0; i < g_img_import_choices.size(); i++) {
        if (g_img_import_choices[i].preview_tex) {
            SDL_DestroyTexture(g_img_import_choices[i].preview_tex);
            g_img_import_choices[i].preview_tex = NULL;
        }
        g_img_import_choices[i].preview_tried = false;
    }
}

static void img_import_choices_clear(void)
{
    img_import_discard_preview_textures();
    g_img_import_choices.clear();
}

static SDL_Texture *img_import_build_preview_texture(const ImgImportChoice &choice)
{
    if (!g_rend || !choice.valid || choice.rec_index < 0 ||
        choice.w <= 0 || choice.h <= 0)
        return NULL;
    size_t pixels_n = (size_t)choice.w * (size_t)choice.h;
    if (pixels_n == 0 || pixels_n > 2097152u)
        return NULL;

    FILE *f = fopen(g_img_import_picker_path, "rb");
    if (!f) return NULL;
    long file_sz = img_file_size_for_import(f);
    ImgLibHeaderDisk hdr;
    if (file_sz < (long)sizeof hdr || fread(&hdr, 1, sizeof hdr, f) != sizeof hdr ||
        hdr.temp != 0xABCD || hdr.version < 0x0500 ||
        choice.rec_index >= (int)hdr.imgcnt || hdr.oset >= (unsigned int)file_sz) {
        fclose(f);
        return NULL;
    }

    int disk_pal_count = (hdr.palcnt > IMG_NUM_DEFAULT_PALS)
                       ? (int)hdr.palcnt - IMG_NUM_DEFAULT_PALS : 0;
    long rec_end = (long)hdr.oset
                 + (long)hdr.imgcnt * (long)sizeof(ImgImageDisk)
                 + (long)disk_pal_count * (long)sizeof(ImgPaletteDisk);
    if (rec_end > file_sz) {
        fclose(f);
        return NULL;
    }

    ImgImageDisk id;
    long img_rec = (long)hdr.oset + (long)choice.rec_index * (long)sizeof id;
    if (img_rec < 0 || img_rec + (long)sizeof id > file_sz ||
        fseek(f, img_rec, SEEK_SET) != 0 || fread(&id, 1, sizeof id, f) != sizeof id) {
        fclose(f);
        return NULL;
    }
    int w = (id.w < 3) ? 3 : (int)id.w;
    int h = (int)id.h;
    if (w != choice.w || h != choice.h || w <= 0 || h <= 0 ||
        (size_t)w * (size_t)h != pixels_n) {
        fclose(f);
        return NULL;
    }

    Uint32 pal[256];
    for (int i = 0; i < 256; i++) {
        pal[i] = 0xFF000000u | ((Uint32)i << 16) | ((Uint32)i << 8) | (Uint32)i;
    }
    if (g_img_import_index0_transparent)
        pal[0] = 0;

    int pal_offset = (int)id.palnum - IMG_NUM_DEFAULT_PALS;
    if (pal_offset >= 0 && pal_offset < disk_pal_count) {
        ImgPaletteDisk pd;
        long pal_rec = (long)hdr.oset + (long)hdr.imgcnt * (long)sizeof(ImgImageDisk)
                     + (long)pal_offset * (long)sizeof pd;
        if (fseek(f, pal_rec, SEEK_SET) == 0 && fread(&pd, 1, sizeof pd, f) == sizeof pd) {
            int count = (int)pd.numc;
            if (count < 1) count = 1;
            if (count > 256) count = 256;
            if ((long)pd.oset >= 0 && (long)pd.oset + (long)count * 2L <= file_sz &&
                fseek(f, (long)pd.oset, SEEK_SET) == 0) {
                for (int i = 0; i < count; i++) {
                    unsigned char b[2] = {0, 0};
                    if (fread(b, 1, 2, f) != 2) break;
                    unsigned short word = (unsigned short)(b[0] | (b[1] << 8));
                    pal[i] = (i == 0 && g_img_import_index0_transparent)
                           ? 0
                           : img_pal_word_to_argb_opaque(word);
                }
            }
        }
    }

    unsigned char *pix = (unsigned char *)malloc(pixels_n);
    if (!pix) {
        fclose(f);
        return NULL;
    }
    if (!img_decode_pixels(f, file_sz, &id, w, h, pix, NULL, NULL)) {
        free(pix);
        fclose(f);
        return NULL;
    }
    fclose(f);

    std::vector<Uint32> rgba(pixels_n, 0u);
    for (size_t i = 0; i < pixels_n; i++) {
        unsigned char v = pix[i];
        rgba[i] = (v == 0 && g_img_import_index0_transparent) ? 0u : pal[v];
    }
    free(pix);

    SDL_Texture *tex = SDL_CreateTexture(g_rend, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return NULL;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, NULL, rgba.data(), w * (int)sizeof(Uint32));
    return tex;
}

static SDL_Texture *img_import_choice_preview_texture(ImgImportChoice &choice)
{
    if (!choice.preview_tried) {
        choice.preview_tex = img_import_build_preview_texture(choice);
        choice.preview_tried = true;
    }
    return choice.preview_tex;
}

static bool img_import_choice_matches_filter(const ImgImportChoice &c, const char *filter)
{
    if (!filter || !filter[0]) return true;
    if (img_picker_text_contains_ci(c.label, filter)) return true;
    char buf[32];
    snprintf(buf, sizeof buf, "%d", c.w);
    if (img_picker_text_contains_ci(buf, filter)) return true;
    snprintf(buf, sizeof buf, "%d", c.h);
    if (img_picker_text_contains_ci(buf, filter)) return true;
    snprintf(buf, sizeof buf, "%X", c.rec_index);
    if (img_picker_text_contains_ci(buf, filter)) return true;
    return false;
}

void open_img_import_picker(const char *path)
{
    img_import_choices_clear();
    g_img_import_picker_status[0] = '\0';
    snprintf(g_img_import_picker_path, sizeof g_img_import_picker_path, "%s", path ? path : "");
    if (!path || !path[0]) return;

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(g_img_import_picker_status, sizeof g_img_import_picker_status, "Could not open IMG.");
        stage_set_toast(g_img_import_picker_status);
        return;
    }
    long file_sz = img_file_size_for_import(f);
    ImgLibHeaderDisk hdr;
    if (file_sz < (long)sizeof hdr || fread(&hdr, 1, sizeof hdr, f) != sizeof hdr ||
        hdr.temp != 0xABCD || hdr.version < 0x0500 || hdr.imgcnt == 0 ||
        hdr.oset >= (unsigned int)file_sz) {
        fclose(f);
        snprintf(g_img_import_picker_status, sizeof g_img_import_picker_status, "Unsupported IMG.");
        stage_set_toast(g_img_import_picker_status);
        return;
    }

    char base[64];
    img_basename_no_ext_upper(path, base, sizeof base);
    int valid = 0;
    for (int i = 0; i < (int)hdr.imgcnt; i++) {
        ImgImageDisk id;
        long img_rec = (long)hdr.oset + (long)i * (long)sizeof id;
        if (img_rec < 0 || img_rec + (long)sizeof id > file_sz ||
            fseek(f, img_rec, SEEK_SET) != 0 || fread(&id, 1, sizeof id, f) != sizeof id)
            continue;
        ImgImportChoice c;
        memset(&c, 0, sizeof c);
        c.rec_index = i;
        c.w = (id.w < 3) ? 3 : (int)id.w;
        c.h = (int)id.h;
        c.palnum = (int)id.palnum;
        c.anix = img_s16(id.anix);
        c.aniy = img_s16(id.aniy);
        c.flags = (int)id.flags;
        c.valid = c.w > 0 && c.h > 0 && c.w <= 4096 && c.h <= 4096;
        c.selected = c.valid;
        img_raw_name_to_upper(id.name, 16, base, c.label, sizeof c.label);
        if (c.valid) valid++;
        g_img_import_choices.push_back(c);
    }
    fclose(f);

    snprintf(g_img_import_picker_status, sizeof g_img_import_picker_status,
             "Loaded %d sprite(s) from %s.", valid, img_picker_basename_ptr(path));
    g_img_import_picker_filter[0] = '\0';
    g_img_import_picker_open = true;
    g_img_import_picker_request = true;
}

void draw_img_import_picker(void)
{
    if (g_img_import_picker_request) {
        ImGui::OpenPopup("Import IMG Sprites");
        g_img_import_picker_request = false;
    }
    if (!g_img_import_picker_open) return;

    bool modal_open = true;
    ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Import IMG Sprites", &modal_open)) {
        ImGui::Text("%s", img_picker_basename_ptr(g_img_import_picker_path));
        if (g_img_import_picker_status[0])
            ImGui::TextDisabled("%s", g_img_import_picker_status);
        ImGui::Separator();

        ImGui::Checkbox("Optimize selected after import", &g_import_optimize_after_import);
        ImGui::SameLine();
        ImGui::Checkbox("Trim", &g_import_opt_trim);
        ImGui::SameLine();
        ImGui::Checkbox("Compact palettes", &g_import_opt_compact_palettes);
        if (ImGui::Checkbox("Treat IMG index 0 as transparent", &g_img_import_index0_transparent))
            img_import_discard_preview_textures();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("MK2 IMG sprites normally use index 0 as transparent matte. Turn this off only if color 0 is intentional visible art.");
        ImGui::Checkbox("Skip labels already in BDD", &g_import_skip_existing_labels);
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("Filter", g_img_import_picker_filter, sizeof g_img_import_picker_filter);

        int visible = 0, selected = 0, existing_selected = 0;
        for (size_t i = 0; i < g_img_import_choices.size(); i++) {
            ImgImportChoice &c = g_img_import_choices[i];
            if (!img_import_choice_matches_filter(c, g_img_import_picker_filter)) continue;
            visible++;
            if (c.selected) {
                selected++;
                if (img_label_exists_ci(c.label)) existing_selected++;
            }
        }

        if (ImGui::SmallButton("Select Visible")) {
            for (size_t i = 0; i < g_img_import_choices.size(); i++)
                if (img_import_choice_matches_filter(g_img_import_choices[i], g_img_import_picker_filter))
                    g_img_import_choices[i].selected = g_img_import_choices[i].valid;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Select New")) {
            for (size_t i = 0; i < g_img_import_choices.size(); i++) {
                ImgImportChoice &c = g_img_import_choices[i];
                if (!img_import_choice_matches_filter(c, g_img_import_picker_filter)) continue;
                c.selected = c.valid && !img_label_exists_ci(c.label);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Visible")) {
            for (size_t i = 0; i < g_img_import_choices.size(); i++)
                if (img_import_choice_matches_filter(g_img_import_choices[i], g_img_import_picker_filter))
                    g_img_import_choices[i].selected = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d visible, %d checked%s", visible, selected,
                            existing_selected ? " with existing labels" : "");

        if (ImGui::BeginTable("img_import_picker", 8,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(0, 360))) {
            ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 38.0f);
            ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 38.0f);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 78.0f);
            ImGui::TableSetupColumn("Pal", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Origin", ImGuiTableColumnFlags_WidthFixed, 82.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 88.0f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < g_img_import_choices.size(); i++) {
                ImgImportChoice &c = g_img_import_choices[i];
                if (!img_import_choice_matches_filter(c, g_img_import_picker_filter)) continue;
                ImGui::PushID((int)i);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 54.0f);
                ImGui::TableNextColumn();
                if (!c.valid) ImGui::BeginDisabled();
                ImGui::Checkbox("##use", &c.selected);
                if (!c.valid) ImGui::EndDisabled();
                ImGui::TableNextColumn();
                if (SDL_Texture *tex = img_import_choice_preview_texture(c)) {
                    float sc = 48.0f / (float)(c.w > c.h ? c.w : c.h);
                    if (sc > 2.0f) sc = 2.0f;
                    if (sc <= 0.0f) sc = 1.0f;
                    draw_editor_texture_transparent(tex, c.w * sc, c.h * sc);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn(); ImGui::Text("%d", c.rec_index);
                ImGui::TableNextColumn(); ImGui::Text("%s", c.label);
                ImGui::TableNextColumn(); ImGui::Text("%dx%d", c.w, c.h);
                ImGui::TableNextColumn(); ImGui::Text("%d", c.palnum);
                ImGui::TableNextColumn(); ImGui::Text("%d,%d", c.anix, c.aniy);
                ImGui::TableNextColumn();
                if (!c.valid)
                    ImGui::TextColored(ImVec4(1.0f,0.35f,0.25f,1.0f), "bad size");
                else if (img_label_exists_ci(c.label))
                    ImGui::TextColored(ImVec4(1.0f,0.75f,0.25f,1.0f), "exists");
                else
                    ImGui::TextDisabled("new");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        int selected_len = 0;
        for (const ImgImportChoice &choice : g_img_import_choices)
            if (choice.rec_index + 1 > selected_len)
                selected_len = choice.rec_index + 1;
        std::vector<unsigned char> selected_flags((size_t)(selected_len > 0 ? selected_len : 0), 0);
        int final_count = 0;
        for (size_t i = 0; i < g_img_import_choices.size(); i++) {
            ImgImportChoice &c = g_img_import_choices[i];
            bool use = c.selected && c.valid;
            if (use && g_import_skip_existing_labels && img_label_exists_ci(c.label))
                use = false;
            if (c.rec_index >= 0 && c.rec_index < selected_len)
                selected_flags[(size_t)c.rec_index] = use ? 1 : 0;
            if (use) final_count++;
        }

        if (final_count <= 0) ImGui::BeginDisabled();
        if (ImGui::Button("Import Selected", ImVec2(150, 0))) {
            int n = import_img_file_filtered(g_img_import_picker_path, true,
                                             selected_flags.data(), selected_len);
            if (n > 0) {
                g_img_import_picker_open = false;
                img_import_choices_clear();
            }
        }
        if (final_count <= 0) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) {
            g_img_import_picker_open = false;
            img_import_choices_clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d will import", final_count);
        ImGui::EndPopup();
    }
    if (!modal_open) {
        g_img_import_picker_open = false;
        img_import_choices_clear();
    }
}
