#ifndef GEVR_BEAN_CE_H
#define GEVR_BEAN_CE_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The GoldenEye XBLA Community Edition over the player's imported Bean files
 * (gevr_bean_ce.c).
 *
 * zip is the updater as picked (CommunityEditionUpdaterV6.zip); it is read and
 * closed. Its files.diff is applied to beanDir/files/ and what it writes is
 * kept in beanDir/ce/files/. Returns the number of files written, or -1 with
 * the reason in msg.
 */
int gevrBeanCeApply(FILE *zip, const char *beanDir, char *msg, size_t msgLen);

/* Whether the update has been applied to the imported copy. */
int gevrBeanCeApplied(void);

/*
 * The folder the Bean loaders read before the release's own, laid out as its
 * files/ folder; NULL when the update is not applied or is switched off
 * (VrBeanCommunityEdition).
 */
const char *gevrBeanCeRoot(void);

#ifdef __cplusplus
}
#endif

#endif
