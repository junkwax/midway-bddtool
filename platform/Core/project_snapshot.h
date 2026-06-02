#ifndef PROJECT_SNAPSHOT_H
#define PROJECT_SNAPSHOT_H

#include "../bdd_format.h"

#ifndef __cplusplus
#include <stdbool.h>
#endif

struct ProjectSnapshot {
    int image_capacity;
    int object_capacity;
    int palette_capacity;
    int module_capacity;
    Img *img;
    int ni;
    Obj *obj;
    int *obj_lock;
    int *obj_hidden;
    int no;
    Uint32 (*pals)[256];
    int *pal_count;
    int n_pals;
    char (*pal_name)[64];
    char (*bdb_modules)[256];
    int bdb_num_modules;
    char bdb_header[256];
    int have_bdb;
    char name[64];
};

int project_snapshot_clamp_count(int n, int cap);
int project_snapshot_min_count(int a, int b);
void project_snapshot_free_pixels(ProjectSnapshot *snapshot);
void project_snapshot_release(ProjectSnapshot *snapshot);
bool project_snapshot_ensure_storage(ProjectSnapshot *snapshot, bool include_object_flags);
bool project_snapshot_capture_current(ProjectSnapshot *snapshot, bool include_object_flags);
void project_snapshot_restore_current(const ProjectSnapshot *snapshot, bool include_object_flags);

#endif
