#include "UI/tools/stage_share_bundle.h"
#include "UI/tools/stage_share_media.h"

#include "bg_editor.h"
#include "bg_editor_globals.h"
#include "Core/path_utils.h"
#include "Core/stage_paths.h"
#include "Core/zip_writer.h"
#include "imgui.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

/* Defined in Core/import_export.cpp; declared here rather than pulling in
   import_export.h, which collides with bg_editor_globals.h. */
bool export_composite_to(const char *dest_path);
bool copy_file_overwrite(const char *src, const char *dst);

bool g_show_stage_share = false;

char g_share_author[96] = "";
char g_share_contact[96] = "";
char g_share_description[512] = "";
char g_share_credits[512] = "";
char g_share_license[96] = "Free to use in MK2 builds";
bool g_share_tested_mame = false;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void md_append(std::string &s, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    s += buf;
}

static const char *share_stage_name(void)
{
    if (g_name[0]) return g_name;
    if (g_bdb_path[0]) return path_basename_ptr(g_bdb_path);
    return "UNTITLED";
}

static void url_encode(const char *in, std::string &out)
{
    static const char *hex = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)in; p && *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~')
            out += (char)*p;
        else {
            out += '%';
            out += hex[*p >> 4];
            out += hex[*p & 0xF];
        }
    }
}

static void open_in_shell(const char *target)
{
#ifdef _WIN32
    ShellExecuteA(NULL, "open", target, NULL, NULL, SW_SHOWNORMAL);
#else
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", target ? target : "");
    if (system(cmd) != 0) { /* best effort */ }
#endif
}

/* ------------------------------------------------------------------ */
/* page sections                                                       */
/* ------------------------------------------------------------------ */

static void section_credits(std::string &md, const char *name)
{
    md += "## Credits\n\n";
    md += "| | |\n|---|---|\n";
    md_append(md, "| **Author** | %s |\n",
              g_share_author[0] ? g_share_author : "_unattributed_");
    if (g_share_contact[0])
        md_append(md, "| **Contact** | [@%s](https://github.com/%s) |\n",
                  g_share_contact, g_share_contact);
    md_append(md, "| **Stage name** | `%s` |\n", name);
    md_append(md, "| **Tested in MAME** | %s |\n", g_share_tested_mame ? "yes" : "not reported");
    if (g_share_license[0])
        md_append(md, "| **Use** | %s |\n", g_share_license);
    md += "\n";
    if (g_share_credits[0])
        md_append(md, "**Credits / sources:** %s\n\n", g_share_credits);
}

/* The How-to-enable section is generated from the stage's real BGND.ASM
   bindings, so a reader gets this stage's planes, rates and floor rather than
   a generic recipe they have to adapt. */
static void section_enable(std::string &md, const char *bdb_file, const char *bdd_file)
{
    md += "## How to enable\n\n";
    md += "These steps target an `mk2-main` source tree. The values below were read from\n";
    md += "the stage as authored -- substitute your own bg id if it collides.\n\n";

    md += "### 1. Put the files in `data/`\n\n";
    md_append(md, "Copy `%s` and `%s` into `mk2-main/data/`.\n\n", bdb_file, bdd_file);

    md += "### 2. Add it to the background `.LOD`\n\n";
    md += "In `data/BGNDTEST.LOD` (or whichever `.LOD` builds your backgrounds), add:\n\n";
    md_append(md, "```\nBBB> %s\n```\n\n", share_stage_name());
    md += "LOAD2 then emits this stage's `*HDRS`, `*BLKS` and `*PALS` into\n";
    md += "`BGNDTBL.ASM` / `BGNDPAL.ASM` / `BGNDEQU.H`.\n\n";

    md += "### 3. Declare the stage in `BGND.ASM`\n\n";
    int planes = bdd_stage_plane_count();
    if (planes > 0) {
        md += "Plane bindings this stage was authored against:\n\n";
        md += "| baklst | Module | Offset x,y | Parallax | Draw rank |\n";
        md += "|---:|---|---|---:|---:|\n";
        for (int i = 0; i < planes; i++) {
            char pname[64] = "";
            int ox = 0, oy = 0, rank = -1;
            float scroll = 0.0f;
            if (!bdd_stage_plane_info(i, pname, sizeof pname, &ox, &oy, &scroll, &rank))
                continue;
            md_append(md, "| %d | `%sBMOD` | %d, %d | %.4gx | %d |\n",
                      bdd_stage_module_baklst(pname), pname, ox, oy, (double)scroll, rank);
        }
        md += "\nParallax is relative to the fighter plane (`0x20000` = 2.0 px/frame).\n";
        md += "Rates above 1.0 are foreground planes.\n\n";
    } else {
        md += "This stage has no `<stage>_mod` block in a reachable `BGND.ASM` yet, so the\n";
        md += "plane bindings need writing by hand -- one module per `baklst`, eight max:\n\n";
        md += "| baklst | Module |\n|---:|---|\n";
        for (int m = 0; m < g_bdb_num_modules; m++) {
            char mname[64] = "";
            sscanf(g_bdb_modules[m], "%63s", mname);
            md_append(md, "| %d | `%sBMOD` |\n", m + 1, mname);
        }
        md += "\n";
    }

    int ground = 0, cam_x = 0, cam_y = 0, left = 0, right = 0, bg = 0;
    bool have_ground = bdd_get_stage_ground_y(&ground) != 0;
    bool have_cam = bdd_get_stage_start_camera(&cam_x, &cam_y) != 0;
    bool have_limits = bdd_get_stage_scroll_limits(&left, &right) != 0;
    bool have_bg = bdd_get_stage_bg_color(&bg) != 0;

    md += "`<stage>_mod` header words:\n\n```\n";
    md_append(md, "\t.word\t0%Xh\t\t; autoerase / background colour%s\n",
              have_bg ? bg : 0, have_bg ? "" : "   (set yours)");
    md_append(md, "\t.word\t%d\t\t; ground y%s\n", have_ground ? ground : 0,
              have_ground ? "" : "   (not captured)");
    md_append(md, "\t.word\t%d\t\t; initial world y%s\n", have_cam ? cam_y : 0,
              have_cam ? "" : "   (not captured)");
    md_append(md, "\t.word\t%d\t\t; initial world x%s\n", have_cam ? cam_x : 0,
              have_cam ? "" : "   (not captured)");
    md_append(md, "\t.word\t%d\t\t; scroll left limit\n", have_limits ? left : 0);
    md_append(md, "\t.word\t%d\t\t; scroll right limit\n", have_limits ? right : 0);
    md += "```\n\n";

    md += "### 4. Give it a bg id\n\n";
    md += "Add the stage's `_mod` label to `table_o_mods` in `BGND.ASM`. The entry's index\n";
    md += "is the bg id passed to `do_a11_background`.\n\n";

    char floor_label[32] = "", floor_pal[32] = "";
    int floor_y = 0, floor_h = 0;
    md += "### 5. Floor\n\n";
    if (bdd_stage_floor_descriptor(floor_label, sizeof floor_label,
                                   floor_pal, sizeof floor_pal, &floor_y, &floor_h)) {
        md_append(md, "Uses the skewed perspective floor `%s` (palette `%s`, screen y %d,\n"
                      "%d lines). That bitmap is **not** in the BDD/BDB -- it is a 1200 px wide\n"
                      "6bpp strip loaded via `FRM>` into `MKFLOORS.TBL`, and needs a\n"
                      "`<stage>_floor_info` block plus a `-1,floor_code` entry in the `dlists`.\n\n",
                  floor_label, floor_pal[0] ? floor_pal : "?", floor_y, floor_h);
    } else {
        md += "No skew floor -- the ground is ordinary blocks on the fighter plane. No\n";
        md += "`<stage>_floor_info` or `floor_code` dlists entry needed.\n\n";
    }

    md += "### 6. Palettes and build\n\n";
    md_append(md, "%d palette(s). Run **Tools > Sync MK2 Runtime Palettes** in bddtool (or merge\n"
                  "the generated `BGNDPAL.ASM` entries by hand), then run LOAD2 over the `.LOD`\n"
                  "and rebuild. Only 16 hardware palette slots exist at runtime, shared with the\n"
                  "fighters.\n\n", g_n_pals);
}

static void section_budget(std::string &md)
{
    Mk2Diag d;
    mk2_collect_diag(&d);

    md += "## Budget\n\n";
    md += "| Check | Value | Limit |\n|---|---:|---:|\n";
    md_append(md, "| Modules (planes) | %d | %d |\n", g_bdb_num_modules, MK2_LOAD2_MAX_MODULES);
    md_append(md, "| Blocks | %d | %d |\n", g_no, MK2_LOAD2_MAX_BLOCKS);
    md_append(md, "| Image headers | %d | %d |\n", g_ni, MK2_LOAD2_MAX_IMAGE_HEADERS);
    md_append(md, "| Palettes | %d | %d |\n", g_n_pals, MK2_LOAD2_MAX_STAGE_PALETTES);
    md_append(md, "| Largest block | %d bytes | %d |\n",
              d.max_load2_block_bytes, MK2_LOAD2_MAX_DATA_BYTES);
    md_append(md, "| Peak display objects | %d | %d |\n",
              d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP);
    md += "\n";

    int hard = mk2_diag_hard_issues(&d);
    if (hard > 0)
        md_append(md, "> ⚠️ **%d hard LOAD2 issue(s)** reported by the editor.\n\n", hard);
    else
        md += "> No hard LOAD2 issues reported by the editor.\n\n";

    md += "See the [Stage Author's Checklist](Stage-Authors-Checklist) for what these\n";
    md += "limits mean.\n\n";
}

static void build_page(std::string &md, const char *name,
                       const char *bdb_file, const char *bdd_file,
                       bool have_layout, bool have_game,
                       const char *gif_file, const char *mp4_file,
                       const SharePropInfo *props, int prop_count)
{
    md_append(md, "# %s\n\n", name);
    md += "[Home](Home) › [Stage Catalog](Stage-Catalog) › ";
    md_append(md, "%s\n\n", name);

    if (g_share_description[0])
        md_append(md, "%s\n\n", g_share_description);

    md += "**Contents:** ";
    if (mp4_file && mp4_file[0]) md += "[Video](#video) · ";
    md += "[Arena](#arena) · ";
    if (gif_file && gif_file[0]) md += "[Animated Arena](#animated-arena) · ";
    if (prop_count > 0) md += "[Props](#props) · ";
    md += "[How to enable](#how-to-enable) · [Budget](#budget)\n\n";

    section_credits(md, name);
    md += "---\n\n";

    if (mp4_file && mp4_file[0]) {
        md += "## Video\n\n";
        md_append(md, "The camera panning the full width of the stage and back, every frame at\n"
                      "full size — the smoothest look at this stage: [`%s`](%s)\n\n",
                  mp4_file, mp4_file);
    }

    md += "## Arena\n\n";
    if (have_layout) {
        md += "The whole stage, all planes flattened:\n\n";
        md += "![arena layout](arena_layout.png)\n\n";
    }
    if (have_game) {
        md += "In-game framing (400×254) at the stage's start camera:\n\n";
        md += "![arena in game](arena_game.png)\n\n";
    }

    if (gif_file && gif_file[0]) {
        md += "## Animated Arena\n\n";
        md += "The same pan with the real per-plane parallax, at reduced size and frame rate\n";
        md += "so it stays a reasonable download";
        if (mp4_file && mp4_file[0])
            md += " — see [Video](#video) for the full-size, every-frame version";
        md += ":\n\n";
        md_append(md, "![scrolling](%s)\n\n", gif_file);
    }

    if (prop_count > 0) {
        md += "## Props\n\n";
        md_append(md, "Every image in the BDD (%d), at native size. Index 0 is transparent.\n\n",
                  prop_count);
        md += "| | Image | Size | Palette |\n|---|---:|---:|---:|\n";
        for (int i = 0; i < prop_count; i++) {
            md_append(md, "| ![%d](props/%s) | `0x%02X` | %d×%d | %d |\n",
                      props[i].idx, props[i].file, props[i].idx,
                      props[i].w, props[i].h, props[i].pal);
        }
        md += "\n";
    }

    section_enable(md, bdb_file, bdd_file);
    section_budget(md);

    md += "---\n\n";
    md += "Generated by [midway-bddtool](" STAGE_SHARE_REPO_URL ") — **File > Share Stage...**\n";
}

/* ------------------------------------------------------------------ */
/* bundle                                                              */
/* ------------------------------------------------------------------ */

int stage_share_build_bundle(const char *out_dir, int scroll_frames,
                             char *zip_path, size_t zip_path_sz,
                             char *page_path, size_t page_path_sz,
                             char *err, size_t err_sz)
{
    if (zip_path && zip_path_sz) zip_path[0] = '\0';
    if (page_path && page_path_sz) page_path[0] = '\0';
    if (err && err_sz) err[0] = '\0';

    if (!out_dir || !out_dir[0]) {
        snprintf(err, err_sz, "no output folder");
        return -1;
    }
    /* A never-saved project only has a placeholder file name, so there is
       nothing on disk to copy into the bundle. */
    if (!project_save_location_is_set()) {
        snprintf(err, err_sz, "save the stage first -- a share bundle needs the BDB/BDD on disk");
        return -1;
    }
    if (!ensure_dir_recursive(out_dir)) {
        snprintf(err, err_sz, "cannot create %s", out_dir);
        return -1;
    }

    const char *name = share_stage_name();
    int written = 0;
    std::vector<std::string> zip_files;   /* absolute path | name inside zip */
    std::vector<std::string> zip_names;

    char bdb_dst[700] = "", bdd_dst[700] = "";
    if (g_bdb_path[0]) {
        path_join(bdb_dst, sizeof bdb_dst, out_dir, path_basename_ptr(g_bdb_path));
        if (copy_file_overwrite(g_bdb_path, bdb_dst)) written++;
        else bdb_dst[0] = '\0';
    }
    if (g_bdd_path[0]) {
        path_join(bdd_dst, sizeof bdd_dst, out_dir, path_basename_ptr(g_bdd_path));
        if (copy_file_overwrite(g_bdd_path, bdd_dst)) written++;
        else bdd_dst[0] = '\0';
    }

    char layout_dst[700];
    path_join(layout_dst, sizeof layout_dst, out_dir, "arena_layout.png");
    bool have_layout = export_composite_to(layout_dst);
    if (have_layout) written++;

    /* The in-game framing needs the renderer, so it comes from the same
       re-invocation path as the animation. */
    char game_dst[700];
    path_join(game_dst, sizeof game_dst, out_dir, "arena_game.png");
    const char *stage_src = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    bool have_game = share_media_render_game_view(stage_src, game_dst);
    if (have_game) written++;

    char props_dir[700];
    path_join(props_dir, sizeof props_dir, out_dir, "props");
    std::vector<SharePropInfo> props((size_t)(g_ni > 0 ? g_ni : 1));
    int prop_count = share_media_export_props(props_dir, props.data(), g_ni);
    written += prop_count;

    char gif_path[700] = "", mp4_path[700] = "";
    if (scroll_frames > 0) {
        share_media_render_scroll(out_dir, stage_src, scroll_frames,
                                  gif_path, sizeof gif_path, mp4_path, sizeof mp4_path);
        if (gif_path[0]) written++;
        if (mp4_path[0]) written++;
    }

    std::string md;
    build_page(md, name,
               bdb_dst[0] ? path_basename_ptr(bdb_dst) : "(no BDB)",
               bdd_dst[0] ? path_basename_ptr(bdd_dst) : "(no BDD)",
               have_layout, have_game,
               gif_path[0] ? "scroll.gif" : "",
               mp4_path[0] ? "scroll.mp4" : "",
               props.data(), prop_count);

    char md_dst[700], md_name[128];
    snprintf(md_name, sizeof md_name, "%s.md", name);
    path_join(md_dst, sizeof md_dst, out_dir, md_name);
    FILE *f = fopen(md_dst, "wb");
    if (!f) {
        snprintf(err, err_sz, "cannot write %s", md_dst);
        return -1;
    }
    fwrite(md.data(), 1, md.size(), f);
    bool md_ok = ferror(f) == 0;
    if (fclose(f) != 0) md_ok = false;
    if (!md_ok) {
        snprintf(err, err_sz, "failed writing %s", md_dst);
        return -1;
    }
    written++;
    if (page_path && page_path_sz) snprintf(page_path, page_path_sz, "%s", md_dst);

    /* One archive so the whole entry is a single drag-and-drop attachment. */
    char zip_dst[700], zip_name[160];
    snprintf(zip_name, sizeof zip_name, "%s-stage.zip", name);
    path_join(zip_dst, sizeof zip_dst, out_dir, zip_name);
    ZipWriter *z = zip_writer_open(zip_dst);
    if (!z) {
        snprintf(err, err_sz, "cannot create %s", zip_dst);
        return -1;
    }
    bool zok = true;
    if (bdb_dst[0]) zok = zip_writer_add_file(z, bdb_dst, path_basename_ptr(bdb_dst)) && zok;
    if (bdd_dst[0]) zok = zip_writer_add_file(z, bdd_dst, path_basename_ptr(bdd_dst)) && zok;
    if (have_layout) zok = zip_writer_add_file(z, layout_dst, "arena_layout.png") && zok;
    if (have_game) zok = zip_writer_add_file(z, game_dst, "arena_game.png") && zok;
    if (gif_path[0]) zok = zip_writer_add_file(z, gif_path, "scroll.gif") && zok;
    if (mp4_path[0]) zok = zip_writer_add_file(z, mp4_path, "scroll.mp4") && zok;
    for (int i = 0; i < prop_count; i++) {
        char src[800], inzip[128];
        path_join(src, sizeof src, props_dir, props[i].file);
        snprintf(inzip, sizeof inzip, "props/%s", props[i].file);
        zok = zip_writer_add_file(z, src, inzip) && zok;
    }
    zok = zip_writer_add_memory(z, md.data(), md.size(), md_name) && zok;
    if (!zip_writer_close(z) || !zok) {
        snprintf(err, err_sz, "failed writing %s", zip_dst);
        return -1;
    }
    written++;
    if (zip_path && zip_path_sz) snprintf(zip_path, zip_path_sz, "%s", zip_dst);

    return written;
}

/* ------------------------------------------------------------------ */
/* submit flow                                                         */
/* ------------------------------------------------------------------ */

static char s_share_dir[700] = "";
static char s_share_zip[700] = "";
static char s_share_page[700] = "";
static char s_share_err[256] = "";
static int  s_share_files = 0;
/* 160 frames over a stage's scroll range is roughly a 2px camera step, which
   reads as a smooth pan rather than a slideshow. */
static int  s_share_frames = 160;
static bool s_share_built = false;

void stage_share_submit_flow(void)
{
    s_share_err[0] = '\0';
    s_share_files = 0;
    s_share_built = false;
    g_show_stage_share = true;
}

static void do_build(void)
{
    char dir[512] = "";
    if (!folder_dialog_open("Choose a folder for the share bundle", dir, sizeof dir))
        return;

    char out_dir[700];
    path_join(out_dir, sizeof out_dir, dir, share_stage_name());
    snprintf(s_share_dir, sizeof s_share_dir, "%s", out_dir);

    s_share_files = stage_share_build_bundle(out_dir, s_share_frames,
                                             s_share_zip, sizeof s_share_zip,
                                             s_share_page, sizeof s_share_page,
                                             s_share_err, sizeof s_share_err);
    s_share_built = s_share_files >= 0;
    if (!s_share_built) {
        snprintf(g_toast_msg, sizeof g_toast_msg, "Share failed: %s", s_share_err);
        g_toast_timer = 4.0f;
    }
}

/* The issue body rides in the URL query string, which browsers cap near 8 KB,
   so it stays a summary -- the full page is in the zip and on the clipboard. */
static void open_submission_issue(const char *name)
{
    Mk2Diag d;
    mk2_collect_diag(&d);

    char body[1800];
    snprintf(body, sizeof body,
             "<!-- Drag %s-stage.zip into this box. -->\n\n"
             "## Stage\n\n"
             "- Name: `%s`\n"
             "- Author: %s\n"
             "- Modules (planes): %d\n"
             "- Blocks: %d\n"
             "- Images: %d\n"
             "- Palettes: %d\n"
             "- Peak on-screen blocks: %d / %d display objects\n"
             "- Editor hard issues: %d\n"
             "- Tested in MAME: %s\n\n"
             "## Description\n\n%s\n\n"
             "## Checklist\n\n"
             "- [ ] `%s-stage.zip` attached (BDB, BDD, arena renders, scroll animation, props, page)\n"
             "- [ ] Loads and saves cleanly in bddtool\n"
             "- [ ] I authored this art, or it is cleared for redistribution\n",
             name, name,
             g_share_author[0] ? g_share_author : "(unattributed)",
             g_bdb_num_modules, g_no, g_ni, g_n_pals,
             d.max_visible_objects, MK2_DISPLAY_OBJECT_CAP,
             mk2_diag_hard_issues(&d),
             g_share_tested_mame ? "yes" : "not reported",
             g_share_description[0] ? g_share_description : "_(none given)_",
             name);

    char title[160];
    snprintf(title, sizeof title, "[stage] %s", name);

    std::string url = STAGE_SHARE_REPO_URL "/issues/new?labels=stage-submission&title=";
    url_encode(title, url);
    url += "&body=";
    url_encode(body, url);
    open_in_shell(url.c_str());
}

void draw_stage_share_dialog(void)
{
    if (!g_show_stage_share) return;
    ImGui::OpenPopup("Share Stage");
    ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Share Stage", &g_show_stage_share,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const char *name = share_stage_name();
    ImGui::Text("Sharing stage: %s", name);
    ImGui::Separator();

    ImGui::TextDisabled("Credits — saved for next time");
    ImGui::InputText("Author", g_share_author, sizeof g_share_author);
    ImGui::InputText("GitHub handle", g_share_contact, sizeof g_share_contact);
    ImGui::InputTextMultiline("Description", g_share_description, sizeof g_share_description,
                              ImVec2(-1, 48));
    ImGui::InputTextMultiline("Credits / sources", g_share_credits, sizeof g_share_credits,
                              ImVec2(-1, 40));
    ImGui::InputText("Use / license", g_share_license, sizeof g_share_license);
    ImGui::Checkbox("Tested in MAME", &g_share_tested_mame);

    ImGui::Separator();
    ImGui::SliderInt("Scroll frames", &s_share_frames, 0, 320);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Frames in the scrolling animation. 0 skips it.\n"
                          "More frames = smoother and slower pan.\n"
                          "Needs ffmpeg on PATH.");
    if (s_share_frames > 0 && !share_media_have_ffmpeg())
        ImGui::TextColored(ImVec4(1, 0.65f, 0.25f, 1),
                           "ffmpeg not found on PATH — the animation will be skipped.");

    ImGui::Separator();
    if (!s_share_built) {
        if (ImGui::Button("Build bundle...", ImVec2(150, 0))) {
            settings_save();
            do_build();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("renders the arena, props and animation");
    } else {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1),
                           "Bundle ready: %d file(s)", s_share_files);
        ImGui::TextWrapped("%s", s_share_dir);
        ImGui::Spacing();
        ImGui::TextWrapped("The wiki cannot accept uploads, so stages are submitted as an "
                           "issue with the zip attached. Accepted stages are published to "
                           "the wiki.");
        ImGui::Spacing();
        if (ImGui::Button("Open submission issue", ImVec2(180, 0)))
            open_submission_issue(name);
        ImGui::SameLine();
        if (ImGui::Button("Open folder", ImVec2(110, 0)))
            open_in_shell(s_share_dir);
        ImGui::SameLine();
        if (ImGui::Button("Copy page", ImVec2(100, 0))) {
            FILE *f = fopen(s_share_page, "rb");
            if (f) {
                std::string text;
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
                fclose(f);
                ImGui::SetClipboardText(text.c_str());
                snprintf(g_toast_msg, sizeof g_toast_msg, "Wiki page copied");
                g_toast_timer = 2.0f;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Rebuild", ImVec2(90, 0)))
            s_share_built = false;
    }

    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(80, 0))) {
        g_show_stage_share = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
