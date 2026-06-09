#ifndef PATH_FIELD_H
#define PATH_FIELD_H

#include <stddef.h>

void draw_path_field(const char *label, char *buf, size_t bufsz,
                     const char *dialog_title, const char *filter);

#endif
