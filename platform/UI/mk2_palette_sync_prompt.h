#ifndef MK2_PALETTE_SYNC_PROMPT_H
#define MK2_PALETTE_SYNC_PROMPT_H

void mk2_palette_sync_request_prompt(const char *reason, bool allow_if_unknown_path);
bool mk2_palette_sync_auto_apply_if_ready(const char *reason);
void draw_mk2_palette_sync_prompt(void);

#endif
