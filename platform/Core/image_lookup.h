#ifndef IMAGE_LOOKUP_H
#define IMAGE_LOOKUP_H

#include "bdd_format.h"

#ifdef __cplusplus
extern "C" {
#endif

Img *img_find(int idx);
void img_free(void);

#ifdef __cplusplus
}
#endif

#endif
