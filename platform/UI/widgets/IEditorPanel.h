#pragma once

#include <string>

enum class EditorPanelRegion {
    MainMenu,
    PrimaryToolbar,
    DocumentTabs,
    RightRail,
    Workspace
};

class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;

    // Returns a unique identifier for the panel
    virtual const char* get_name() const = 0;

    // Called every frame to render the panel's ImGui interface
    virtual void render() = 0;

    // Returns the render band where this panel belongs
    virtual EditorPanelRegion region() const { return EditorPanelRegion::Workspace; }

    // Returns whether the panel should be visible
    virtual bool is_visible() const { return true; }
};
