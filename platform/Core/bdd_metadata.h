#ifndef BDD_METADATA_H
#define BDD_METADATA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BddImageMetadata {
    int idx;
    int anix;
    int aniy;
    int anix2;
    int aniy2;
    int aniz2;
    int frm;
    int opals;
    int pttblnum;
    int lod_ref;
    char label[64];
    char source[64];
} BddImageMetadata;

int bdd_metadata_save_records(const char *bdd_path,
                              const BddImageMetadata *records,
                              int record_count);
int bdd_metadata_load_records(const char *bdd_path,
                              BddImageMetadata *records,
                              int record_count);

void editor_project_save_bdd_metadata(const char *bdd_path);
void editor_project_load_bdd_metadata(const char *bdd_path);

#ifdef __cplusplus
}
#endif

#endif
