#ifndef TOAST_NOTIFICATIONS_H
#define TOAST_NOTIFICATIONS_H

extern float g_toast_timer;
extern char g_toast_msg[128];

void stage_set_toast(const char *msg);
void draw_stage_toast_overlay(void);
void draw_toasts(void);

#endif
