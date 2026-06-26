#include "bg_editor_globals.h"

#include <imgui.h>

struct Mk2LayerPreset {
    const char *label;
    int wx;
};

/* Every value here is a real depth byte ((wx>>8)&0xFF, BAKGND.ASM calls it map_z)
 * found by scanning the shipped stage BDBs in mk2-readonly/mk2-main/data — not
 * invented. This byte is paint/depth-sort order ONLY (which block draws in
 * front of which other block) — it does not control scroll speed. Confirmed
 * against the original loader (doc/load2/ldbgnd2.c: blocks join a module by
 * spatial containment, never by z) and live BGND.ASM scroll tables (each
 * stage's baklst 1-8 has its own hand-authored rate, e.g. dedpool_scroll vs
 * tower_scroll use entirely different values for the same slot numbers).
 * Real scroll speed is a per-module property -- set it in Game Preview's
 * Runtime Parallax controls, not here. */
static const Mk2LayerPreset k_mk2_layers[] = {
    {"0x32  Sky/back (depth)",      0x32},
    {"0x3B  Mid- (depth)",          0x3B},
    {"0x3C  Mid (depth)",           0x3C},
    {"0x3D  Mid+ (depth)",          0x3D},
    {"0x3E  Mid++ (depth)",         0x3E},
    {"0x3F  Mid max (depth)",       0x3F},
    {"0x40  Floor/play (depth)",    0x40},
    {"0x41  Floor alt (depth)",     0x41},
    {"0x42  Near- (depth)",         0x42},
    {"0x43  Near FG (depth)",       0x43},
    {"0x44  Near FG+ (depth)",      0x44},
    {"0x45  Near FG++ (depth)",     0x45},
    {"0x46  Front FG (depth)",      0x46},
    {"0x47  Front FG+ (depth)",     0x47},
    {"0x48  Front FG++ (depth)",    0x48},
    {"0x49  Front max (depth)",     0x49},
    {"0x4B  Extreme FG (depth)",    0x4B},
    {"0x4E  Max FG (depth)",        0x4E},
};

static const int k_mk2_layer_count = sizeof(k_mk2_layers) / sizeof(k_mk2_layers[0]);

const char *layer_friendly_name(int layer_byte)
{
    switch (layer_byte) {
        case 0x32: return "Sky";
        case 0x3B: return "Mid-";
        case 0x3C: return "Mid";
        case 0x3D: return "Mid+";
        case 0x3E: return "Mid++";
        case 0x3F: return "Mid max";
        case 0x40: return "Floor";
        case 0x41: return "Floor+";
        case 0x42: return "Near-";
        case 0x43: return "Near FG";
        case 0x44: return "Near FG+";
        case 0x45: return "Near FG++";
        case 0x46: return "Front FG";
        case 0x47: return "Front FG+";
        case 0x48: return "Front FG++";
        case 0x49: return "Front max";
        case 0x4B: return "Extreme FG";
        case 0x4E: return "Max FG";
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
                           "Floor layer: main ground depth. Scroll speed is set per-module in Runtime Binding, not here.");
    } else if (layer == 0x41) {
        ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                           "Floor alt: same depth tier as the floor, useful for playfield props.");
    } else {
        ImGui::TextDisabled("Floor art usually uses Floor/play 0x40. Modules only group export regions.");
    }
}
