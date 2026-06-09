#include "Core/editor_commands.h"

#include "bg_editor_globals.h"

#include <vector>

void editor_process_commands(void)
{
    std::vector<EditorCommand> commands;
    editor_drain_commands(commands);

    for (const EditorCommand& command : commands) {
        switch (command.type) {
        case EditorCommandType::None:
            break;
        case EditorCommandType::UnsavedAction:
            request_unsaved_action(command.action,
                                   command.path.empty() ? nullptr : command.path.c_str(),
                                   command.doc_idx);
            break;
        case EditorCommandType::SaveAll:
            save_all_project();
            break;
        case EditorCommandType::ShowVerify:
            g_show_verify = true;
            break;
        case EditorCommandType::ShowPreferences:
            g_show_prefs = true;
            break;
        case EditorCommandType::OpenMk2Tool:
            open_mk2_tool(command.value);
            break;
        }
    }
}
