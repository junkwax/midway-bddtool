/* Post-save staleness warning for generated MK2 LOD artifacts.

   Saving a BDB/BDD pair that an active LOD lists with a BBB> record leaves
   the LOAD2 outputs (IRW payload, BGNDTBL stock records, copied SAGs) stale
   until a full build regenerates them. Updating only BDB/BDD plus BGNDPAL
   is not sufficient; see mk2asset docs\BDDTOOL_FOREST2_SAVE_CONTRACT.md. */

#include "bg_editor_globals.h"
#include "Core/app_diagnostics.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#define lod_stale_strcasecmp _stricmp
#define lod_stale_strncasecmp _strnicmp
#else
#include <dirent.h>
#include <strings.h>
#define lod_stale_strcasecmp strcasecmp
#define lod_stale_strncasecmp strncasecmp
#endif

static bool s_lod_stale_pending = false;
static char s_lod_stale_stage[64] = "";
static char s_lod_stale_lods[160] = "";
static char s_lod_stale_detail[1600] = "";

static void lod_stale_stage_key(char *out, size_t outsz)
{
    out[0] = '\0';
    char nm[64] = "";
    int ww, wh, md, nmodes, np, no;
    if (g_bdb_header[0] &&
        sscanf(g_bdb_header, "%63s %d %d %d %d %d %d", nm, &ww, &wh, &md, &nmodes, &np, &no) >= 1)
        snprintf(out, outsz, "%s", nm);
    else if (g_name[0])
        snprintf(out, outsz, "%s", g_name);
    else if (g_bdb_path[0] || g_bdd_path[0]) {
        const char *base = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
        for (const char *p = base; *p; p++)
            if (*p == '\\' || *p == '/') base = p + 1;
        snprintf(out, outsz, "%s", base);
        char *dot = strrchr(out, '.');
        if (dot) *dot = '\0';
    }
    for (char *p = out; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
}

/* True when the LOAD2 script lists the stage with a "BBB> <stage>" record. */
static bool lod_stale_lod_references_stage(const char *lod_path, const char *stage)
{
    FILE *f = fopen(lod_path, "rb");
    if (!f) return false;
    bool hit = false;
    char line[512];
    while (!hit && fgets(line, sizeof line, f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (lod_stale_strncasecmp(p, "BBB>", 4) != 0) continue;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
        char tok[64];
        size_t n = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && n + 1 < sizeof tok)
            tok[n++] = *p++;
        tok[n] = '\0';
        if (tok[0] && lod_stale_strcasecmp(tok, stage) == 0)
            hit = true;
    }
    fclose(f);
    return hit;
}

/* A LOD is part of the active build when its generated IRW sits next to it
   in data\; test LODs such as BGNDTEST have no IRW and are skipped. */
static bool lod_stale_lod_is_active(const char *dir, const char *lod_base)
{
    char irw_name[96], irw_path[640];
    snprintf(irw_name, sizeof irw_name, "%s.IRW", lod_base);
    path_join(irw_path, sizeof irw_path, dir, irw_name);
    return stage_file_exists(irw_path);
}

static void lod_stale_append_lod(char *list, size_t listsz, const char *lod_base)
{
    size_t len = strlen(list);
    snprintf(list + len, listsz > len ? listsz - len : 0, "%s%s",
             len ? ", " : "", lod_base);
}

static int lod_stale_collect_active_lods(const char *dir, const char *stage,
                                         char *list, size_t listsz,
                                         char *first_lod, size_t first_lodsz)
{
    list[0] = '\0';
    first_lod[0] = '\0';
    int found = 0;
#ifdef _WIN32
    char pattern[560];
    snprintf(pattern, sizeof pattern, "%s\\*.LOD", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        char base[96];
        snprintf(base, sizeof base, "%s", fd.cFileName);
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        char full[640];
        path_join(full, sizeof full, dir, fd.cFileName);
        if (!lod_stale_lod_references_stage(full, stage)) continue;
        if (!lod_stale_lod_is_active(dir, base)) continue;
        if (!found) snprintf(first_lod, first_lodsz, "%s", base);
        lod_stale_append_lod(list, listsz, base);
        found++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 4 || lod_stale_strcasecmp(name + len - 4, ".LOD") != 0)
            continue;
        char base[96];
        snprintf(base, sizeof base, "%s", name);
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        char full[1024];
        path_join(full, sizeof full, dir, name);
        if (!lod_stale_lod_references_stage(full, stage)) continue;
        if (!lod_stale_lod_is_active(dir, base)) continue;
        if (!found) snprintf(first_lod, first_lodsz, "%s", base);
        lod_stale_append_lod(list, listsz, base);
        found++;
    }
    closedir(d);
#endif
    return found;
}

void mk2_lod_stale_check_after_save(void)
{
    if (!g_mk2_lod_stale_warn_after_save) return;

    char stage[64];
    lod_stale_stage_key(stage, sizeof stage);
    if (!stage[0]) return;

    const char *project_path = g_bdd_path[0] ? g_bdd_path : g_bdb_path;
    if (!project_path || !project_path[0]) return;
    char dir[512];
    stage_dirname(project_path, dir, sizeof dir);
    if (!dir[0]) return;

    char lods[160], first_lod[96];
    if (!lod_stale_collect_active_lods(dir, stage, lods, sizeof lods,
                                       first_lod, sizeof first_lod))
        return;

    snprintf(s_lod_stale_stage, sizeof s_lod_stale_stage, "%s", stage);
    snprintf(s_lod_stale_lods, sizeof s_lod_stale_lods, "%s", lods);
    snprintf(s_lod_stale_detail, sizeof s_lod_stale_detail,
             "Stage %s is listed by active LOD(s): %s\n"
             "\n"
             "The saved BDB/BDD pair updates the stage source, but these\n"
             "generated build artifacts stay stale until LOAD2 regenerates them:\n"
             "  data\\%s.IRW\n"
             "  src\\BGNDTBL.ASM (HDRS/BLKS/BMOD stock records)\n"
             "  tmp\\load2\\BGNDTBL.MK7\n"
             "  src\\%s.TBL\n"
             "  src\\MKSEL.ASM (copied VS-restore SAGs)\n"
             "  program/video ROM outputs\n"
             "\n"
             "Run the full pipeline from the mk2 tree before packaging or\n"
             "testing in MAME:\n"
             "  python build.py\n"
             "  python makerom.py\n"
             "  python makevrom.py --debug-bin\n"
             "  python mamerom.py --no-install\n"
             "\n"
             "build.py --asm-only is not enough: only the full build runs LOAD2\n"
             "and promotes complete BGNDTBL stock records (BLKS + BMOD + HDRS).",
             stage, lods, first_lod, first_lod);
    bdd_save_logf("post-save stale warning: stage=%s lods=%s; run python build.py before packaging",
                  stage, lods);
    stage_set_toast("Saved; MK2 LOD build artifacts are now stale");
    s_lod_stale_pending = true;
}

void draw_mk2_lod_stale_warning(void)
{
    /* Wait until other popups (palette sync, save error) are dismissed so the
       two post-save modals do not cancel each other in the popup stack. */
    if (s_lod_stale_pending && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
        ImGui::OpenPopup("MK2 Build Artifacts Stale");
        s_lod_stale_pending = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("MK2 Build Artifacts Stale", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s saved, but %s generated artifacts are stale. "
                           "Run python build.py from the mk2 tree before packaging or testing in MAME.",
                           s_lod_stale_stage, s_lod_stale_lods);
        ImGui::Separator();
        ImGui::InputTextMultiline("##lod_stale_detail", s_lod_stale_detail,
                                  sizeof s_lod_stale_detail,
                                  ImVec2(560, 280), ImGuiInputTextFlags_ReadOnly);
        ImGui::Checkbox("Warn after Save", &g_mk2_lod_stale_warn_after_save);
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
