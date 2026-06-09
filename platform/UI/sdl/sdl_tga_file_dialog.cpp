#include "UI/sdl/sdl_tga_file_dialog.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <commdlg.h>
#endif

int bdd_sdl_open_tga_file_dialog(char *out, int outsz)
{
    if (!out || outsz <= 0) return 0;

#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = "TGA Files\0*.TGA;*.tga\0All Files\0*.*\0";
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = (DWORD)outsz;
    ofn.lpstrTitle  = "Load TGA";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? 1 : 0;
#else
    const char *cmds[] = {
        "zenity --file-selection --title='Load TGA' "
            "--file-filter='TGA files (*.tga *.TGA) | *.tga *.TGA' 2>/dev/null",
        "kdialog --getopenfilename . '*.tga *.TGA' 2>/dev/null",
        NULL
    };

    for (int i = 0; cmds[i]; i++) {
        FILE *p = popen(cmds[i], "r");
        if (!p) continue;
        char buf[512] = "";
        fgets(buf, sizeof buf, p);
        pclose(p);
        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0]) {
            snprintf(out, (size_t)outsz, "%s", buf);
            return 1;
        }
    }
    return 0;
#endif
}
