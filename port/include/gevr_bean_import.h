#ifndef GEVR_BEAN_IMPORT_H
#define GEVR_BEAN_IMPORT_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bean from the player's own GoldenEye XBLA copy (gevr_bean_import.c).
 *
 * src is the copy as picked: an archive of the unpacked release, the Xbox 360
 * package, or an archive holding the package. It is read and closed. The files
 * the Bean loaders read are written under dest/files/, nothing else of the
 * copy is kept. Returns the number of files written, or -1 with the reason in
 * msg.
 */
int gevrBeanImportStream(FILE *src, const char *dest, char *msg, size_t msgLen);

/*
 * One entry of a zip, the first whose path ends in tail (at a folder, either
 * separator, any case), written to path. src is read and closed. 0 when it
 * was, else -1 with the reason in msg. Progress and cancel are the import's.
 */
int gevrBeanImportZipEntry(FILE *src, const char *tail, const char *path, char *msg, size_t msgLen);

/* The running import's progress, 0..100, for another thread to draw. */
int gevrBeanImportPercent(void);

/* Asks the running import to stop; it returns -1 with "Cancelled." */
void gevrBeanImportCancel(void);

/*
 * Where an entry of the release goes, relative to dest: its path from its
 * files/ folder on, when it is one a Bean loader reads. 0 for the rest.
 */
int gevrBeanImportWanted(const char *name, char *out, size_t outLen);

#ifdef __cplusplus
}
#endif

#endif
