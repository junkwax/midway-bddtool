#include "UI/panels/RightRailPanels.h"
#include "UI/view/right_sidebar.h"

#include "bg_editor_globals.h"

void RightSidebarPanel::render()
{
    draw_right_sidebar();
}

void BlockEditorPanel::render()
{
    draw_block_editor();
}

void SpriteResizePanel::render()
{
    draw_sprite_resize_dialog();
}

void SplitObjectPanel::render()
{
    draw_split_object_dialog();
}
