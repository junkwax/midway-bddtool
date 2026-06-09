#pragma once

#include "UI/IEditorPanel.h"

class ToolboxPanel : public IEditorPanel {
public:
    ToolboxPanel() = default;
    ~ToolboxPanel() override = default;

    const char* get_name() const override { return "Toolbox"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::PrimaryToolbar; }
    void render() override;
};
