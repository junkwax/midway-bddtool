#include "bg_editor_globals.h"
#include "imgui.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static char g_diff_a_path[512] = "";
static char g_diff_b_path[512] = "";
static char g_diff_out_path[512] = "tmp\\mk2_preview_diff.png";
static int g_diff_threshold = 8;
static int g_diff_mismatches = 0;
static int g_diff_w = 0;
static int g_diff_h = 0;
static float g_diff_avg_abs = 0.0f;
static char g_diff_status[160] = "";

static void set_diff_source_preview(void)
{
    char fname[128];
    snprintf(fname, sizeof fname, "%s_preview.png", g_stage_internal_name);
    resolve_stage_file(g_diff_a_path, sizeof g_diff_a_path, fname);
}

static void set_diff_rom_preview(char *dst, size_t dstsz)
{
    char path[512];
    resolve_stage_file(path, sizeof path, g_stage_rom_preview);
    snprintf(dst, dstsz, "%s", path);
}

static void set_diff_current_composite(char *dst, size_t dstsz)
{
    snprintf(dst, dstsz, "%s", g_bdb_path);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = '\0';
    stage_append(dst, dstsz, "_composite.png");
}

static void ensure_tmp_dir(void)
{
#ifdef _WIN32
    CreateDirectoryA("tmp", NULL);
#else
    mkdir("tmp", 0755);
#endif
}

static int run_png_diff(const char *a_path, const char *b_path, const char *out_path, int threshold)
{
    int aw = 0, ah = 0, an = 0, bw = 0, bh = 0, bn = 0;
    unsigned char *a = stbi_load(a_path, &aw, &ah, &an, 4);
    unsigned char *b = stbi_load(b_path, &bw, &bh, &bn, 4);
    if (!a || !b) {
        if (a) stbi_image_free(a);
        if (b) stbi_image_free(b);
        snprintf(g_diff_status, sizeof g_diff_status, "Could not load one or both PNGs");
        return 0;
    }
    int w = aw < bw ? aw : bw;
    int h = ah < bh ? ah : bh;
    if (w <= 0 || h <= 0) {
        stbi_image_free(a);
        stbi_image_free(b);
        snprintf(g_diff_status, sizeof g_diff_status, "Images have no overlapping area");
        return 0;
    }
    unsigned char *out = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
    if (!out) {
        stbi_image_free(a);
        stbi_image_free(b);
        snprintf(g_diff_status, sizeof g_diff_status, "Out of memory for diff");
        return 0;
    }

    int mismatches = 0;
    Uint64 total_abs = 0;
    int thr = threshold < 0 ? 0 : threshold;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int ai = (y * aw + x) * 4;
            int bi = (y * bw + x) * 4;
            int oi = (y * w + x) * 4;
            int dr = abs((int)a[ai + 0] - (int)b[bi + 0]);
            int dg = abs((int)a[ai + 1] - (int)b[bi + 1]);
            int db = abs((int)a[ai + 2] - (int)b[bi + 2]);
            int da = abs((int)a[ai + 3] - (int)b[bi + 3]);
            int d = dr + dg + db;
            total_abs += (Uint64)d;
            bool bad = dr > thr || dg > thr || db > thr || da > thr;
            if (bad) {
                mismatches++;
                out[oi + 0] = 255;
                out[oi + 1] = (unsigned char)(dg > 255 ? 255 : dg);
                out[oi + 2] = (unsigned char)(db > 255 ? 255 : db);
                out[oi + 3] = 255;
            } else {
                unsigned char grey = (unsigned char)(((int)b[bi + 0] + (int)b[bi + 1] + (int)b[bi + 2]) / 6);
                out[oi + 0] = grey;
                out[oi + 1] = grey;
                out[oi + 2] = grey;
                out[oi + 3] = 255;
            }
        }
    }

    ensure_tmp_dir();
    int ok = stbi_write_png(out_path, w, h, 4, out, w * 4);
    free(out);
    stbi_image_free(a);
    stbi_image_free(b);

    g_diff_mismatches = mismatches;
    g_diff_w = w;
    g_diff_h = h;
    g_diff_avg_abs = (float)((double)total_abs / (double)(w * h * 3));
    snprintf(g_diff_status, sizeof g_diff_status, "%d mismatch pixel(s), avg abs %.2f over %dx%d",
             g_diff_mismatches, g_diff_avg_abs, g_diff_w, g_diff_h);
    if (ok)
        clear_preview_image_file_texture();
    return ok != 0;
}

void draw_mk2_preview_diff_tool(void)
{
    ImGui::Text("ROM Preview Diff");
    ImGui::TextDisabled("Compare source/editor/ROM/live PNGs. Red pixels are differences above threshold.");
    draw_path_field("A PNG", g_diff_a_path, sizeof g_diff_a_path,
                    "Select first PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0");
    draw_path_field("B PNG", g_diff_b_path, sizeof g_diff_b_path,
                    "Select second PNG", "PNG Files\0*.PNG;*.png\0All Files\0*.*\0");
    ImGui::InputText("Diff PNG", g_diff_out_path, sizeof g_diff_out_path);
    ImGui::SliderInt("Threshold", &g_diff_threshold, 0, 64);

    if (ImGui::Button("A = Source Preview")) set_diff_source_preview();
    ImGui::SameLine();
    if (ImGui::Button("A = Composite")) set_diff_current_composite(g_diff_a_path, sizeof g_diff_a_path);
    if (ImGui::Button("B = ROM Preview")) set_diff_rom_preview(g_diff_b_path, sizeof g_diff_b_path);
    ImGui::SameLine();
    if (ImGui::Button("B = Live MAME")) snprintf(g_diff_b_path, sizeof g_diff_b_path, "%s", g_mame_output);

    if (ImGui::Button("Run PNG Diff", ImVec2(-1, 0))) {
        if (run_png_diff(g_diff_a_path, g_diff_b_path, g_diff_out_path, g_diff_threshold))
            stage_set_toast("Preview diff written");
        else
            stage_set_toast("Preview diff failed");
    }

    if (g_diff_status[0]) {
        if (g_diff_mismatches == 0)
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1), "%s", g_diff_status);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1), "%s", g_diff_status);
    }
    draw_preview_image_file("Diff output", g_diff_out_path);
}

void mk2_preview_diff_use_source_and_rom(const char *source_preview, const char *rom_preview)
{
    snprintf(g_diff_a_path, sizeof g_diff_a_path, "%s", source_preview ? source_preview : "");
    snprintf(g_diff_b_path, sizeof g_diff_b_path, "%s", rom_preview ? rom_preview : "");
}

void mk2_preview_diff_use_composite_and_rom(const char *composite, const char *rom_preview)
{
    snprintf(g_diff_a_path, sizeof g_diff_a_path, "%s", composite ? composite : "");
    snprintf(g_diff_b_path, sizeof g_diff_b_path, "%s", rom_preview ? rom_preview : "");
}

void mk2_preview_diff_use_rom_and_mame(const char *rom_preview, const char *mame_output)
{
    snprintf(g_diff_a_path, sizeof g_diff_a_path, "%s", rom_preview ? rom_preview : "");
    snprintf(g_diff_b_path, sizeof g_diff_b_path, "%s", mame_output ? mame_output : "");
}
