#include "Core/editor_app_globals.h"
#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "UI/view/toast_notifications.h"
#include "imgui.h"

#include <stdio.h>

struct Toast {
    char msg[128];
    float ttl;
    int sev;
};

#define TOAST_MAX 6

static Toast g_toasts[TOAST_MAX] = {};
static int g_toast_n = 0;
static int g_toast_warned_obj = 0;
static int g_toast_warned_img = 0;

float g_toast_timer = 0.0f;
char g_toast_msg[128] = "";

static void toast_push(const char *msg, int sev = 0, float ttl = 4.0f)
{
    if (g_toast_n < TOAST_MAX) {
        snprintf(g_toasts[g_toast_n].msg, 128, "%s", msg);
        g_toasts[g_toast_n].ttl = ttl;
        g_toasts[g_toast_n].sev = sev;
        g_toast_n++;
    }
}

void stage_set_toast(const char *msg)
{
    snprintf(g_toast_msg, sizeof g_toast_msg, "%s", msg ? msg : "");
    g_toast_timer = 3.0f;
}

void draw_stage_toast_overlay(void)
{
    if (g_toast_timer <= 0) return;

    g_toast_timer -= ImGui::GetIO().DeltaTime;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImVec2 ts = ImGui::CalcTextSize(g_toast_msg);
    ImVec2 pos(ds.x / 2 - ts.x / 2, ds.y - 60);
    float alpha = (g_toast_timer < 0.5f) ? (g_toast_timer / 0.5f) : 1.0f;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(pos.x - 8, pos.y - 4), ImVec2(pos.x + ts.x + 8, pos.y + ts.y + 4),
                      IM_COL32(30,30,50,(int)(200*alpha)), 4);
    dl->AddText(pos, IM_COL32(200,220,255,(int)(255*alpha)), g_toast_msg);
}

void draw_toasts(void)
{
    if (!g_simple_mode || g_toast_n == 0) return;
    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float y = ds.y - 28.0f;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    int alive = 0;
    for (int i = 0; i < g_toast_n; i++) {
        g_toasts[i].ttl -= dt;
        if (g_toasts[i].ttl <= 0) continue;
        g_toasts[alive++] = g_toasts[i];
    }
    g_toast_n = alive;
    for (int i = 0; i < g_toast_n; i++) {
        Toast *t = &g_toasts[i];
        float alpha = t->ttl < 1.0f ? t->ttl : 1.0f;
        ImVec2 ts = ImGui::CalcTextSize(t->msg);
        float px = (ds.x - ts.x) / 2.0f - 8;
        ImU32 bg = t->sev == 2 ? IM_COL32(180,40,40,(int)(200*alpha)) :
                   t->sev == 1 ? IM_COL32(160,120,20,(int)(200*alpha)) :
                                 IM_COL32(30,80,160,(int)(200*alpha));
        dl->AddRectFilled(ImVec2(px - 4, y - 4), ImVec2(px + ts.x + 12, y + ts.y + 4), bg, 4.0f);
        dl->AddText(ImVec2(px + 4, y), IM_COL32(240,240,240,(int)(255*alpha)), t->msg);
        y -= ts.y + 10;
    }
}

void capacity_warn_check(void)
{
    if (!g_simple_mode) return;
    int object_cap = editor_project_object_capacity();
    int image_cap = editor_project_image_capacity();
    if (object_cap > 0 && !g_toast_warned_obj && g_no > (int)(object_cap * 0.8f)) {
        char m[128];
        snprintf(m, sizeof m, "Stage is 80%% full (%d/%d sprites)", g_no, object_cap);
        toast_push(m, 1);
        g_toast_warned_obj = 1;
    }
    if (image_cap > 0 && !g_toast_warned_img && g_ni > (int)(image_cap * 0.8f)) {
        char m[128];
        snprintf(m, sizeof m, "Image bank 80%% full (%d/%d)", g_ni, image_cap);
        toast_push(m, 1);
        g_toast_warned_img = 1;
    }
    if (object_cap > 0 && g_no <= (int)(object_cap * 0.7f)) g_toast_warned_obj = 0;
    if (image_cap > 0 && g_ni <= (int)(image_cap  * 0.7f)) g_toast_warned_img = 0;
}
