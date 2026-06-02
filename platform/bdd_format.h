#ifndef BDD_FORMAT_H
#define BDD_FORMAT_H

#include "Core/bdd_core.h"
#include <SDL.h>

/* Safe limits — well within the BDB/BDD format's 65535-word cap */
#define MAX_IMAGES   BDD_CORE_MAX_IMAGES
#define MAX_OBJECTS  BDD_CORE_MAX_OBJECTS
#define MAX_MODULES  BDD_CORE_MAX_MODULES
#define MAX_PALS     BDD_CORE_MAX_PALS

typedef struct {
    int    idx;
    int    w, h;
    int    flags;
    int    pal_idx;
    int    anix, aniy;
    int    anix2, aniy2, aniz2;
    int    frm;
    int    opals;
    int    pttblnum;
    int    lod_ref;
    char   label[64];
    char   source[64];
    Uint8 *pix;
} Img;

typedef struct {
    int wx;
    int depth;
    int sy;
    int ii;
    int fl;
    int hfl;
    int vfl;
    int order;
} Obj;

#endif
