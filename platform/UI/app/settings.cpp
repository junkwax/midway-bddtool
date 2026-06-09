#include "bg_editor_globals.h"

#include <cstdio>

void settings_save(void)
{
    FILE *sf = fopen("bddview_settings.cfg", "w");
    if (!sf) return;

    fprintf(sf, "4 %d %d %d %d %d %d %.3f %d %d",
            g_welcome_show ? 1 : 0, g_simple_mode ? 1 : 0,
            g_pref_grid_sx, g_pref_grid_sy,
            g_pref_snap_dist, g_pref_autosave_s, g_pref_font_scale,
            g_layer_tint ? 1 : 0,
            g_pref_autoload_runtime_extras ? 1 : 0);
    fclose(sf);
}

void settings_load(void)
{
    FILE *wf = fopen("bddview_settings.cfg", "r");
    if (!wf) return;

    int ver = 0;
    if (fscanf(wf, "%d", &ver) == 1 && (ver == 3 || ver == 4)) {
        int v1 = 1, v2 = 1, gsx = 8, gsy = 8, snp = 8, asv = 60;
        int tlt = 0, auto_runtime = 0;
        float fsc = 1.0f;
        int n = fscanf(wf, "%d %d %d %d %d %d %f %d %d",
                       &v1, &v2, &gsx, &gsy, &snp, &asv, &fsc, &tlt,
                       &auto_runtime);
        if (n >= 2) {
            g_welcome_show = (v1 != 0);
            g_simple_mode = (v2 != 0);
            g_pref_grid_sx = gsx > 0 ? gsx : 8;
            g_pref_grid_sy = gsy > 0 ? gsy : 8;
            g_pref_snap_dist = snp >= 0 ? snp : 8;
            g_pref_autosave_s = asv >= 0 ? asv : 60;
            g_pref_font_scale = (fsc >= 0.5f && fsc <= 3.0f) ? fsc : 1.0f;
            g_layer_tint = (tlt != 0);
            if (n >= 9)
                g_pref_autoload_runtime_extras = (auto_runtime != 0);
            g_runtime_autoload_pref_loaded = true;
        }
    } else {
        rewind(wf);
        int v1 = 1, v2 = 1;
        int n = fscanf(wf, "%d %d", &v1, &v2);
        if (n >= 1) g_welcome_show = (v1 != 0);
        if (n >= 2) g_simple_mode = (v2 != 0);
    }
    fclose(wf);
}

void settings_load_runtime_autoload_pref_once(void)
{
    if (g_runtime_autoload_pref_loaded) return;
    g_runtime_autoload_pref_loaded = true;

    FILE *wf = fopen("bddview_settings.cfg", "r");
    if (!wf) return;
    int ver = 0;
    if (fscanf(wf, "%d", &ver) == 1 && (ver == 3 || ver == 4)) {
        int v1 = 1, v2 = 1, gsx = 8, gsy = 8, snp = 8, asv = 60;
        int tlt = 0, auto_runtime = 0;
        float fsc = 1.0f;
        int n = fscanf(wf, "%d %d %d %d %d %d %f %d %d",
                       &v1, &v2, &gsx, &gsy, &snp, &asv, &fsc, &tlt,
                       &auto_runtime);
        if (n >= 9)
            g_pref_autoload_runtime_extras = (auto_runtime != 0);
    }
    fclose(wf);
}
