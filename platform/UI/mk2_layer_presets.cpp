#include "bg_editor_globals.h"

#include <imgui.h>

struct Mk2LayerPreset {
    const char *label;
    int wx;
};

static const Mk2LayerPreset k_mk2_layers[] = {
    {"0x32  Sky/back 0.2x",    0x32},
    {"0x3C  Mid depth 0.5x",   0x3C},
    {"0x40  Floor/play 1.0x",  0x40},
    {"0x41  Floor alt 1.0x",   0x41},
    {"0x43  Near FG 1.2x",     0x43},
    {"0x46  Front FG 1.5x",    0x46},
};

static const int k_mk2_layer_count = sizeof(k_mk2_layers) / sizeof(k_mk2_layers[0]);

const char *layer_friendly_name(int layer_byte)
{
    switch (layer_byte) {
        case 0x32: return "Sky";
        case 0x3C: return "Mid";
        case 0x40: return "Floor";
        case 0x41: return "Floor+";
        case 0x43: return "Near FG";
        case 0x46: return "Front FG";
        default:
            return "Custom";
    }
}

const char *layer_role_name(int role)
{
    switch (role) {
        case 0: return "background";
        case 1: return "mid/temple";
        case 2: return "floor";
        case 3: return "foreground";
        case 4: return "fx-only";
        default: return "custom";
    }
}

const char *mk2_layer_label(int wx)
{
    for (int i = 0; i < k_mk2_layer_count; i++)
        if (k_mk2_layers[i].wx == wx) return k_mk2_layers[i].label;
    return "custom";
}

int mk2_layer_preset_count(void)
{
    return k_mk2_layer_count;
}

const char *mk2_layer_preset_label(int index)
{
    if (index < 0 || index >= k_mk2_layer_count) return "";
    return k_mk2_layers[index].label;
}

int mk2_layer_preset_wx(int index)
{
    if (index < 0 || index >= k_mk2_layer_count) return 0x40;
    return k_mk2_layers[index].wx;
}

void draw_layer_role_hint(int layer)
{
    if (layer == 0x40) {
        ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.65f, 1.0f),
                           "Floor layer: normal camera speed. Use this for the main ground.");
    } else if (layer == 0x41) {
        ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                           "Floor alt: same speed as the floor, useful for playfield props.");
    } else {
        ImGui::TextDisabled("Floor art usually uses Floor/play 0x40. Modules only group export regions.");
    }
}
