#pragma once

#include "UI/widgets/IEditorPanel.h"

class ToolbarPanel : public IEditorPanel {
public:
    ToolbarPanel() = default;
    ~ToolbarPanel() override = default;

    const char* get_name() const override { return "Toolbar"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::PrimaryToolbar; }
    void render() override;
};
