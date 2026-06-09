#include "UI/object_position_undo.h"

#include "Core/editor_project_globals.h"
#include "Core/editor_project_storage.h"
#include "undo_manager.h"

bool object_position_undo_capture_selected(ObjectPositionUndoCapture *capture)
{
    if (!capture) return false;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return false;

    capture->capacity = object_cap;
    capture->before_depth.assign((size_t)object_cap, 0);
    capture->before_sy.assign((size_t)object_cap, 0);
    capture->mask.assign((size_t)object_cap, 0);

    bool any = false;
    for (int i = 0; i < g_no && i < object_cap; i++) {
        capture->before_depth[i] = g_obj[i].depth;
        capture->before_sy[i] = g_obj[i].sy;
        if (g_sel_flags[i]) {
            capture->mask[i] = 1;
            any = true;
        }
    }
    if (!any) {
        capture->capacity = 0;
        capture->before_depth.clear();
        capture->before_sy.clear();
        capture->mask.clear();
    }
    return any;
}

bool object_position_undo_capture_mask(ObjectPositionUndoCapture *capture,
                                       const unsigned char *mask)
{
    if (!capture || !mask) return false;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return false;

    capture->capacity = object_cap;
    capture->before_depth.assign((size_t)object_cap, 0);
    capture->before_sy.assign((size_t)object_cap, 0);
    capture->mask.assign((size_t)object_cap, 0);

    bool any = false;
    for (int i = 0; i < g_no && i < object_cap; i++) {
        capture->before_depth[i] = g_obj[i].depth;
        capture->before_sy[i] = g_obj[i].sy;
        if (mask[i]) {
            capture->mask[i] = 1;
            any = true;
        }
    }
    if (!any) {
        capture->capacity = 0;
        capture->before_depth.clear();
        capture->before_sy.clear();
        capture->mask.clear();
    }
    return any;
}

int object_position_undo_commit(const ObjectPositionUndoCapture *capture,
                                const char *label)
{
    if (!capture || capture->capacity <= 0)
        return 0;
    return undo_save_object_position_delta_for_mask(capture->before_depth.data(),
                                                   capture->before_sy.data(),
                                                   capture->mask.data(),
                                                   capture->capacity,
                                                   label);
}

bool object_record_undo_capture_selected(ObjectRecordUndoCapture *capture)
{
    if (!capture) return false;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return false;

    capture->capacity = object_cap;
    capture->before_objects.assign((size_t)object_cap, Obj{});
    capture->mask.assign((size_t)object_cap, 0);

    bool any = false;
    for (int i = 0; i < g_no && i < object_cap; i++) {
        capture->before_objects[i] = g_obj[i];
        if (g_sel_flags[i]) {
            capture->mask[i] = 1;
            any = true;
        }
    }
    if (!any) {
        capture->capacity = 0;
        capture->before_objects.clear();
        capture->mask.clear();
    }
    return any;
}

bool object_record_undo_capture_mask(ObjectRecordUndoCapture *capture,
                                     const unsigned char *mask)
{
    if (!capture || !mask) return false;
    int object_cap = editor_project_object_capacity();
    if (object_cap <= 0) return false;

    capture->capacity = object_cap;
    capture->before_objects.assign((size_t)object_cap, Obj{});
    capture->mask.assign((size_t)object_cap, 0);

    bool any = false;
    for (int i = 0; i < g_no && i < object_cap; i++) {
        capture->before_objects[i] = g_obj[i];
        if (mask[i]) {
            capture->mask[i] = 1;
            any = true;
        }
    }
    if (!any) {
        capture->capacity = 0;
        capture->before_objects.clear();
        capture->mask.clear();
    }
    return any;
}

int object_record_undo_commit(const ObjectRecordUndoCapture *capture,
                              const char *label)
{
    if (!capture || capture->capacity <= 0)
        return 0;
    return undo_save_object_record_delta_for_mask(capture->before_objects.data(),
                                                 capture->mask.data(),
                                                 capture->capacity,
                                                 label);
}
