/* Shared MK2 path globals that outlived the removed preview/MAME tools.
   Still referenced by the stage preview dashboard, ROM preview diff, and the
   palette-sync prompt, so their storage lives here in the tracked build. */
#include "Core/mk2_shared_paths.h"

char g_mame_output[512] = "tmp\\mame_match_snap\\latest_lua_match.png";
char g_runtime_palette_asm[512] = "";
char g_runtime_bgnd[512] = "";
