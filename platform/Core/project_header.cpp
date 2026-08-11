#include "Core/project_header.h"

#include "Core/editor_project_globals.h"

#include <cstdio>

/* The BDB header is "<name> <w> <h> <depth> <modules> <palettes> <objects>",
 * read back with sscanf("%63s %d %d %d %d %d %d"). A space anywhere in the name
 * shifts every field after it, so whitespace never reaches a stored stage name. */
void sanitize_stage_name(char *out, size_t outsz, const char *name)
{
    if (!out || outsz == 0) return;
    snprintf(out, outsz, "%s", (name && name[0]) ? name : "UNTITLED");
    for (char *p = out; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            *p = '_';
    if (!out[0])
        snprintf(out, outsz, "UNTITLED");
}

void sync_bdb_header_counts(void)
{
    if (!g_have_bdb || !g_bdb_header[0]) return;
    char nm[64] = "";
    int ww = 0, wh = 0, md = 255, old_nm = 0, old_np = 0, old_no = 0;
    if (sscanf(g_bdb_header, "%63s %d %d %d %d %d %d",
               nm, &ww, &wh, &md, &old_nm, &old_np, &old_no) >= 7) {
        snprintf(g_bdb_header, sizeof g_bdb_header, "%s %d %d %d %d %d %d",
                 nm, ww, wh, md, g_bdb_num_modules, g_n_pals, g_no);
    }
}
