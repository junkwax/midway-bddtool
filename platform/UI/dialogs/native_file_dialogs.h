#ifndef NATIVE_FILE_DIALOGS_H
#define NATIVE_FILE_DIALOGS_H

bool file_dialog_open(const char *title, const char *filter, char *out, int outsz);
bool file_dialog_save(const char *title, const char *filter, char *out, int outsz);
bool folder_dialog_open(const char *title, char *out, int outsz);

#endif
