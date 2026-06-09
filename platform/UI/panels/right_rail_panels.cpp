#include "UI/panels/RightRailPanels.h"

#include "bg_editor_globals.h"

void draw_palette(void);

void ObjectListPanel::render()
{
    draw_obj_list();
}

void ObjectPropertiesPanel::render()
{
    draw_obj_properties();
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

void PalettePanel::render()
{
    draw_palette();
}

bool ModulesPanel::is_visible() const
{
    return g_show_modules;
}

void ModulesPanel::render()
{
    draw_modules();
}

bool LayersPanel::is_visible() const
{
    return g_show_layers;
}

void LayersPanel::render()
{
    draw_layers();
}

bool ImageListPanel::is_visible() const
{
    return g_show_images;
}

void ImageListPanel::render()
{
    draw_image_list();
}

bool MinimapPanel::is_visible() const
{
    return g_show_minimap;
}

void MinimapPanel::render()
{
    draw_minimap();
}
