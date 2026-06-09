#pragma once

#include "UI/widgets/IEditorPanel.h"
#include <vector>
#include <memory>

class PanelManager {
public:
    PanelManager() = default;
    ~PanelManager() = default;

    // Registers a panel with the manager
    void register_panel(std::unique_ptr<IEditorPanel> panel);

    // Renders all visible panels
    void render_panels();
    void render_panels(EditorPanelRegion region);

    // Clears all panels
    void clear();

private:
    std::vector<std::unique_ptr<IEditorPanel>> m_panels;
};

// Global instance (or we can inject this into App, but for now we expose a global or App-level)
extern PanelManager g_panel_manager;
