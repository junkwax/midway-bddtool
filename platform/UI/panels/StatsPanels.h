#pragma once

#include "UI/widgets/IEditorPanel.h"

class LevelStatsPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "LevelStats"; }
    void render() override;
};

class BppPreviewPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "BppPreview"; }
    void render() override;
};
