#ifndef BDD_METADATA_H
#define BDD_METADATA_H

#ifdef __cplusplus
extern "C" {
#endif

void editor_project_save_bdd_metadata(const char *bdd_path);
void editor_project_load_bdd_metadata(const char *bdd_path);

#ifdef __cplusplus
}
#endif

#endif
