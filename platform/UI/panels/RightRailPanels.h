#pragma once

#include "UI/widgets/IEditorPanel.h"

/* The fixed right sidebar: objects, images, palettes, modules and stage views,
   each a tab, each split into sub-tabs. */
class RightSidebarPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "RightSidebar"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    void render() override;
};

class BlockEditorPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "BlockEditor"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    void render() override;
};

class SpriteResizePanel : public IEditorPanel {
public:
    const char* get_name() const override { return "SpriteResize"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    void render() override;
};

class SplitObjectPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "SplitObject"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    void render() override;
};
