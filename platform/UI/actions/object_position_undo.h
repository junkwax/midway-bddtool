#ifndef OBJECT_POSITION_UNDO_H
#define OBJECT_POSITION_UNDO_H

#include <vector>

#include "bdd_format.h"

struct ObjectPositionUndoCapture {
    int capacity = 0;
    std::vector<int> before_depth;
    std::vector<int> before_sy;
    std::vector<unsigned char> mask;
};

struct ObjectRecordUndoCapture {
    int capacity = 0;
    std::vector<Obj> before_objects;
    std::vector<unsigned char> mask;
};

bool object_position_undo_capture_selected(ObjectPositionUndoCapture *capture);
bool object_position_undo_capture_mask(ObjectPositionUndoCapture *capture,
                                       const unsigned char *mask);
int object_position_undo_commit(const ObjectPositionUndoCapture *capture,
                                const char *label);

bool object_record_undo_capture_selected(ObjectRecordUndoCapture *capture);
bool object_record_undo_capture_mask(ObjectRecordUndoCapture *capture,
                                     const unsigned char *mask);
int object_record_undo_commit(const ObjectRecordUndoCapture *capture,
                              const char *label);

#endif
