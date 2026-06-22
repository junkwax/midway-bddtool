#include "UI/panels/UtilityPanels.h"
#include "bg_editor_globals.h"

void TilePreviewPanel::render()
{
    draw_tile_preview();
}

void EmptyCanvasHintPanel::render()
{
    draw_empty_canvas_hint();
}

void UndoHistoryPanel::render()
{
    draw_undo_history();
}

void DebugInfoPanel::render()
{
    draw_debug_info();
}

void PaletteAnimationPanel::render()
{
    draw_pal_anim_panel();
}

void GroupBppReducerPanel::render()
{
    draw_group_bpp_reducer_panel();
}
