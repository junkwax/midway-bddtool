#include "UI/PanelManager.h"

#include <utility>

PanelManager g_panel_manager;

void PanelManager::register_panel(std::unique_ptr<IEditorPanel> panel)
{
    if (panel) {
        m_panels.push_back(std::move(panel));
    }
}

void PanelManager::render_panels()
{
    for (auto& panel : m_panels) {
        if (panel->is_visible()) {
            panel->render();
        }
    }
}

void PanelManager::render_panels(EditorPanelRegion region)
{
    for (auto& panel : m_panels) {
        if (panel->is_visible() && panel->region() == region) {
            panel->render();
        }
    }
}

void PanelManager::clear()
{
    m_panels.clear();
}
