#pragma once

#include "UI/widgets/IEditorPanel.h"

class ObjectListPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "ObjectList"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    void render() override;
};

class ObjectPropertiesPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "ObjectProperties"; }
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

class PalettePanel : public IEditorPanel {
public:
    const char* get_name() const override { return "Palette"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    void render() override;
};

class ModulesPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "Modules"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    bool is_visible() const override;
    void render() override;
};

class LayersPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "Layers"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    bool is_visible() const override;
    void render() override;
};

class ImageListPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "ImageList"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    bool is_visible() const override;
    void render() override;
};

class MinimapPanel : public IEditorPanel {
public:
    const char* get_name() const override { return "Minimap"; }
    EditorPanelRegion region() const override { return EditorPanelRegion::RightRail; }
    bool is_visible() const override;
    void render() override;
};
