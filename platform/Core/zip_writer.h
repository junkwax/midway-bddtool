#ifndef ZIP_WRITER_H
#define ZIP_WRITER_H

#include <stddef.h>
#include <stdbool.h>

/* Minimal store-only (method 0) ZIP writer. Stage bundles are a few hundred KB
   at most -- the largest stock BDD is 249 KB -- so the archive exists to make a
   single drag-and-drop attachment, not to save space. */

typedef struct ZipWriter ZipWriter;

ZipWriter *zip_writer_open(const char *path);
/* name_in_zip uses forward slashes and no leading slash. */
bool zip_writer_add_file(ZipWriter *z, const char *src_path, const char *name_in_zip);
bool zip_writer_add_memory(ZipWriter *z, const void *data, size_t len, const char *name_in_zip);
/* Finishes the central directory and closes. Returns false if anything failed
   along the way; the caller should delete a failed archive. */
bool zip_writer_close(ZipWriter *z);

#endif
