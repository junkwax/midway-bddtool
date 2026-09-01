#ifndef RIGHT_SIDEBAR_H
#define RIGHT_SIDEBAR_H

/* Top-level sections of the fixed right sidebar. */
enum {
    SIDEBAR_TAB_OBJECTS = 0,
    SIDEBAR_TAB_IMAGES,
    SIDEBAR_TAB_PALETTES,
    SIDEBAR_TAB_MODULES,
    SIDEBAR_TAB_STAGE,
    SIDEBAR_TAB_COUNT
};

void  draw_right_sidebar(void);

/* Selects a section (and optionally a sub-tab) on the next frame. Used by menus
   and viewport context actions that want to reveal a specific editor. */
void  right_sidebar_show_tab(int tab, int sub_tab);

bool  right_sidebar_visible(void);
float right_sidebar_width(void);      /* current occupied width, grip included */
float editor_canvas_right_x(void);    /* x where usable canvas space ends */
void  right_sidebar_reset_layout(void);

#endif
