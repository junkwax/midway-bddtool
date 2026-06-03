#include "UI/app_icon.h"

#include "stb_image.h"
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <SDL_syswm.h>
#include <windows.h>

#define BDD_APP_ICON_RESOURCE 1
#define BDD_APP_ICON_RESOURCE_TEXT "1"
#define BDD_APP_USER_MODEL_ID L"junkwax.midway-bddtool.bddview"

void bdd_prepare_app_identity(void)
{
    static int prepared = 0;
    HMODULE shell32;

    if (prepared)
        return;
    prepared = 1;

#ifdef SDL_HINT_WINDOWS_INTRESOURCE_ICON
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, BDD_APP_ICON_RESOURCE_TEXT);
#endif
#ifdef SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, BDD_APP_ICON_RESOURCE_TEXT);
#endif

    shell32 = LoadLibraryA("shell32.dll");
    if (shell32) {
        typedef HRESULT (WINAPI *SetAppIdFn)(PCWSTR);
        SetAppIdFn set_app_id = (SetAppIdFn)GetProcAddress(shell32, "SetCurrentProcessExplicitAppUserModelID");
        if (set_app_id)
            set_app_id(BDD_APP_USER_MODEL_ID);
        FreeLibrary(shell32);
    }
}

static int try_set_windows_resource_icon(SDL_Window *win)
{
    static HICON big_icon = NULL;
    static HICON small_icon = NULL;
    SDL_SysWMinfo wm;
    HINSTANCE instance;
    HWND hwnd;

    if (!win)
        return 0;

    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(win, &wm))
        return 0;

    hwnd = wm.info.win.window;
    if (!hwnd)
        return 0;

    instance = GetModuleHandleA(NULL);
    if (!big_icon) {
        big_icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(BDD_APP_ICON_RESOURCE),
                                     IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                     GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    }
    if (!small_icon) {
        small_icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(BDD_APP_ICON_RESOURCE),
                                       IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    }

    if (big_icon) {
        SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big_icon);
        SetClassLongPtrA(hwnd, GCLP_HICON, (LONG_PTR)big_icon);
    }
    if (small_icon) {
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
        SetClassLongPtrA(hwnd, GCLP_HICONSM, (LONG_PTR)small_icon);
    }

    return big_icon || small_icon;
}
#else
void bdd_prepare_app_identity(void)
{
}
#endif

static int try_set_app_icon(SDL_Window *win, const char *path)
{
    int w = 0, h = 0, channels = 0;
    unsigned char *rgba;
    SDL_Surface *surf;

    if (!win || !path || !path[0])
        return 0;

    rgba = stbi_load(path, &w, &h, &channels, 4);
    if (!rgba || w <= 0 || h <= 0)
        return 0;

    surf = SDL_CreateRGBSurfaceFrom(rgba, w, h, 32, w * 4,
                                    0x000000ff, 0x0000ff00,
                                    0x00ff0000, 0xff000000);
    if (surf) {
        SDL_SetWindowIcon(win, surf);
        SDL_FreeSurface(surf);
    }
    stbi_image_free(rgba);
    return surf != NULL;
}

void bdd_set_app_icon(SDL_Window *win)
{
    char path[512];
    char *base = SDL_GetBasePath();
    int png_icon_set = 0;

    if (base) {
        snprintf(path, sizeof path, "%sassets/appicon.png", base);
        png_icon_set = try_set_app_icon(win, path);
        SDL_free(base);
    }

    if (!png_icon_set) png_icon_set = try_set_app_icon(win, "assets/appicon.png");
    if (!png_icon_set) png_icon_set = try_set_app_icon(win, "appicon.png");
    if (!png_icon_set) png_icon_set = try_set_app_icon(win, "platform/assets/appicon.png");

#ifdef _WIN32
    if (try_set_windows_resource_icon(win))
        return;
#endif

    (void)png_icon_set;
}
