#include "UI/dialogs/native_file_dialogs.h"
#include "UI/sdl/sdl_tga_file_dialog.h"

int bdd_sdl_open_tga_file_dialog(char *out, int outsz)
{
    return file_dialog_open("Load TGA", "TGA Files\0*.TGA;*.tga\0All Files\0*.*\0", out, outsz) ? 1 : 0;
}
