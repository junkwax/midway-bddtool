#ifndef NAVIGATION_H
#define NAVIGATION_H

void zoom_to_fit(void);
void zoom_to_selection(void);
void fit_game_preview_zoom_to_window(void);
void focus_editor_on_game_preview_screen(void);
void route_to_game_preview_screen(bool recenter_camera, bool fit_zoom);

#endif
