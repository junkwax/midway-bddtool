#include "UI/panels/RightRailPanels.h"

#include "bg_editor_globals.h"
#include "imgui.h"

void draw_palette(void);

static void draw_object_group_panel(void)
{
    right_panel_set_next(RIGHT_PANEL_OBJECTS);
    bool open = ImGui::Begin("Objects", NULL, ImGuiWindowFlags_HorizontalScrollbar);
    right_panel_after_begin(RIGHT_PANEL_OBJECTS);
    if (!open) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("object_panel_tabs")) {
        if (ImGui::BeginTabItem("Properties")) {
            draw_obj_properties_contents();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Objects")) {
            draw_obj_list_contents();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void ObjectListPanel::render()
{
    draw_object_group_panel();
}

void ObjectPropertiesPanel::render()
{
    /* Properties are grouped with the object list in draw_object_group_panel(). */
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
