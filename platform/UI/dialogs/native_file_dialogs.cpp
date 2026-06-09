#include "compat.h"
#include "UI/native_file_dialogs.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <commdlg.h>
#include <shlobj.h>
#else
#include <cstdio>
#endif

bool file_dialog_open(const char *title, const char *filter,
                      char *out, int outsz)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = (DWORD)outsz;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? true : false;
#else
    (void)filter;
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --title='%s' 2>/dev/null", title ? title : "Open");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getopenfilename . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}

bool file_dialog_save(const char *title, const char *filter,
                      char *out, int outsz)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = (DWORD)outsz;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? true : false;
#else
    (void)filter;
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --save --confirm-overwrite --title='%s' 2>/dev/null",
             title ? title : "Save");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getsavefilename . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}

bool folder_dialog_open(const char *title, char *out, int outsz)
{
#ifdef _WIN32
    char sel[1024] = {0};
    BROWSEINFOA bi = {0};
    bi.lpszTitle = title ? title : "Select folder";
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        bool ok = SHGetPathFromIDListA(pidl, sel) ? true : false;
        CoTaskMemFree(pidl);
        if (ok) {
            snprintf(out, (size_t)outsz, "%s", sel);
            return true;
        }
    }
    out[0] = '\0';
    return false;
#else
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --directory --title='%s' 2>/dev/null",
             title ? title : "Select folder");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getexistingdirectory . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}
