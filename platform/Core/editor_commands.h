#pragma once

#include <string>
#include <vector>

enum class EditorCommandType {
    None,
    UnsavedAction,
    SaveAll,
    ShowVerify,
    ShowPreferences,
    OpenMk2Tool
};

struct EditorCommand {
    EditorCommandType type = EditorCommandType::None;
    int action = 0;
    int value = 0;
    int doc_idx = -1;
    std::string path;
};

void editor_emit_command(const EditorCommand& command);
void editor_emit_unsaved_action(int action, const char* path = nullptr, int doc_idx = -1);
void editor_emit_save_all(void);
void editor_emit_show_verify(void);
void editor_emit_show_preferences(void);
void editor_emit_open_mk2_tool(int tool);
void editor_drain_commands(std::vector<EditorCommand>& out_commands);
void editor_process_commands(void);
void editor_clear_commands(void);
