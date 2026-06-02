/* Shared MK2 path globals that outlived the removed preview/MAME tools.
   Still referenced by the stage preview dashboard, ROM preview diff, and the
   palette-sync prompt, so their storage lives here in the tracked build.
   Include the shared header first so these definitions adopt the same linkage
   the declarations use (the header is shared with the C viewer). */
#include "bg_editor_globals.h"

char g_mame_output[512] = "tmp\\mame_match_snap\\latest_lua_match.png";
char g_runtime_palette_asm[512] = "";
char g_runtime_bgnd[512] = "";
