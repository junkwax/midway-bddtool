#pragma once

#include "UI/widgets/IEditorPanel.h"

class MenuBarPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "MenuBar"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::MainMenu; }
    void render() override;
};
