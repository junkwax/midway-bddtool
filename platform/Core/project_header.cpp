#include "Core/project_header.h"

#include "Core/editor_project_globals.h"

#include <cstdio>

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
