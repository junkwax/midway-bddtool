#pragma once

#include "UI/IEditorPanel.h"

class GarbageCollectPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "GarbageCollect"; }
    void render() override;
};

class CheckpointsPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "Checkpoints"; }
    void render() override;
};

class TilePreviewPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "TilePreview"; }
    void render() override;
};

class EmptyCanvasHintPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "EmptyCanvasHint"; }
    void render() override;
};

class UndoHistoryPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "UndoHistory"; }
    void render() override;
};

class PaletteAnimationPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "PaletteAnimation"; }
    void render() override;
};

class GroupBppReducerPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "GroupBppReducer"; }
    void render() override;
};
