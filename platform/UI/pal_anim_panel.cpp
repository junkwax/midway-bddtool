#include "bg_editor_globals.h"
#include "imgui.h"

#include <string.h>
#include <vector>

struct PalCycle {
    int pal_idx;
    int lo;
    int hi;
    float hz;
    bool active;
};

static PalCycle g_pal_cycles[8] = {};
static int g_pal_cycle_n = 0;
static bool g_pal_anim = false;
static std::vector<Uint32> g_pal_orig;
static float g_pal_accum[8] = {};

bool g_show_pal_anim = false;

bool pal_animation_enabled(void)
{
    return g_pal_anim;
}

static bool ensure_palette_restore_storage(void)
{
    const int pal_cap = editor_project_palette_capacity();
    if (pal_cap <= 0) return false;
    const size_t need = (size_t)pal_cap * 256u;
    if (g_pal_orig.size() != need)
        g_pal_orig.assign(need, 0);
    return true;
}

static bool has_palette_restore_storage(void)
{
    const int pal_cap = editor_project_palette_capacity();
    if (pal_cap <= 0) return false;
    return g_pal_orig.size() == (size_t)pal_cap * 256u;
}

void pal_animation_step(float dt)
{
    if (!g_pal_anim) return;

    bool pal_changed = false;
    for (int ci = 0; ci < g_pal_cycle_n; ci++) {
        PalCycle &c = g_pal_cycles[ci];
        if (!c.active || c.lo < 0 || c.hi <= c.lo ||
            c.pal_idx < 0 || c.pal_idx >= g_n_pals) continue;
        g_pal_accum[ci] += dt * c.hz;
        while (g_pal_accum[ci] >= 1.0f) {
            if (!editor_project_rotate_palette_range(c.pal_idx, c.lo, c.hi))
                break;
            g_pal_accum[ci] -= 1.0f;
            pal_changed = true;
        }
    }
    if (pal_changed) g_need_rebuild = 1;
}

void draw_pal_anim_panel(void)
{
    if (!g_show_pal_anim) return;
    set_left_panel_default(92.0f, 370.0f, 0.0f);
    ImGui::Begin("Palette Animation##palwin", &g_show_pal_anim,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);

    if (ImGui::Checkbox("Enable", &g_pal_anim)) {
        if (g_pal_anim) {
            if (ensure_palette_restore_storage()) {
                const size_t bytes = (size_t)editor_project_palette_capacity() * sizeof(g_pals[0]);
                memcpy(g_pal_orig.data(), g_pals, bytes);
            }
            memset(g_pal_accum, 0, sizeof(g_pal_accum));
        } else {
            if (has_palette_restore_storage()) {
                int pal_count = g_n_pals;
                int pal_cap = editor_project_palette_capacity();
                if (pal_count > pal_cap) pal_count = pal_cap;
                for (int p = 0; p < pal_count; p++) {
                    editor_project_set_palette_slot(
                        p, NULL, g_pal_count[p], g_pal_orig.data() + (size_t)p * 256u);
                }
            }
            g_need_rebuild = 1;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Cycles palette entries each frame to simulate water/fire");

    ImGui::Separator();
    ImGui::TextUnformatted("Cycles:");
    ImGui::SameLine();
    if (ImGui::SmallButton("+") && g_pal_cycle_n < 8) {
        g_pal_cycles[g_pal_cycle_n] = { 0, 16, 31, 8.0f, true };
        g_pal_cycle_n++;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add cycle");

    for (int ci = 0; ci < g_pal_cycle_n; ci++) {
        PalCycle &c = g_pal_cycles[ci];
        ImGui::PushID(ci);
        ImGui::Checkbox("##act", &c.active);
        ImGui::SameLine(0, 4);
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("Pal##p", &c.pal_idx, 0);
        c.pal_idx = (c.pal_idx < 0) ? 0 : (c.pal_idx >= g_n_pals ? g_n_pals - 1 : c.pal_idx);
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(50);
        ImGui::InputInt("Lo##l", &c.lo, 0);
        c.lo = (c.lo < 0) ? 0 : (c.lo > 254 ? 254 : c.lo);
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(50);
        ImGui::InputInt("Hi##h", &c.hi, 0);
        c.hi = (c.hi <= c.lo) ? c.lo + 1 : (c.hi > 255 ? 255 : c.hi);
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(70);
        ImGui::SliderFloat("Hz##z", &c.hz, 1.0f, 60.0f, "%.1f");
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton("X")) {
            for (int j = ci; j < g_pal_cycle_n - 1; j++) g_pal_cycles[j] = g_pal_cycles[j + 1];
            g_pal_cycle_n--;
        }
        ImGui::SameLine(0, 8);
        if (c.pal_idx >= 0 && c.pal_idx < g_n_pals) {
            for (int ei = c.lo; ei <= c.hi && ei < 256; ei++) {
                Uint32 col = g_pals[c.pal_idx][ei];
                ImVec4 cv((float)((col >> 16) & 0xFF) / 255.f,
                           (float)((col >>  8) & 0xFF) / 255.f,
                           (float)( col        & 0xFF) / 255.f, 1.0f);
                ImGui::ColorButton("##sw", cv,
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                    ImVec2(8, 10));
                if (ei < c.hi) ImGui::SameLine(0, 1);
            }
        }
        ImGui::PopID();
    }

    if (g_pal_cycle_n == 0)
        ImGui::TextDisabled("  No cycles defined. Click + to add one.");

    ImGui::End();
}
