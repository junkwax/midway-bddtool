#ifndef NATIVE_FILE_DIALOGS_H
#define NATIVE_FILE_DIALOGS_H

bool file_dialog_open(const char *title, const char *filter, char *out, int outsz);
bool file_dialog_save(const char *title, const char *filter, char *out, int outsz);
/* Same as file_dialog_save, but appends default_ext (no leading dot, e.g. "BDB")
   when the user types a name without one, and uses any string already in `out`
   as the suggested file name. */
bool file_dialog_save_ext(const char *title, const char *filter,
                          const char *default_ext, char *out, int outsz);
bool folder_dialog_open(const char *title, char *out, int outsz);

#endif
