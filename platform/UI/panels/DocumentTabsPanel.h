#pragma once

#include "UI/widgets/IEditorPanel.h"

class DocumentTabsPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "DocumentTabs"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::DocumentTabs; }
    void render() override;
};
