#include "Core/editor_commands.h"

#include <vector>

static std::vector<EditorCommand> g_editor_commands;

void editor_emit_command(const EditorCommand& command)
{
    g_editor_commands.push_back(command);
}

void editor_emit_unsaved_action(int action, const char* path, int doc_idx)
{
    EditorCommand command;
    command.type = EditorCommandType::UnsavedAction;
    command.action = action;
    command.doc_idx = doc_idx;
    if (path && path[0])
        command.path = path;
    editor_emit_command(command);
}

void editor_emit_save_all(void)
{
    EditorCommand command;
    command.type = EditorCommandType::SaveAll;
    editor_emit_command(command);
}

void editor_emit_show_verify(void)
{
    EditorCommand command;
    command.type = EditorCommandType::ShowVerify;
    editor_emit_command(command);
}

void editor_emit_show_preferences(void)
{
    EditorCommand command;
    command.type = EditorCommandType::ShowPreferences;
    editor_emit_command(command);
}

void editor_emit_open_mk2_tool(int tool)
{
    EditorCommand command;
    command.type = EditorCommandType::OpenMk2Tool;
    command.value = tool;
    editor_emit_command(command);
}

void editor_drain_commands(std::vector<EditorCommand>& out_commands)
{
    out_commands.clear();
    out_commands.swap(g_editor_commands);
}

void editor_clear_commands(void)
{
    g_editor_commands.clear();
}
