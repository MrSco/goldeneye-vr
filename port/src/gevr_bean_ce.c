/*
 * The GoldenEye XBLA Community Edition, applied to the player's own imported
 * copy of the release.
 *
 * The Community Edition is a set of fixes the community made to the leaked
 * build: new skydomes, Frigate's water, props, doors, icons and text. It is
 * only ever distributed as patches against the release
 * (CommunityEditionUpdaterV6.zip, HDiffPatch files), and the port keeps to
 * that: nothing of it ships or is downloaded. The player picks the updater's
 * zip on the launcher's Mods page and its files.diff is applied here, once,
 * to the files the import kept (gevr_bean_import.c keeps every file the patch
 * reads from, gevr_bean_ce_refs.inc.h).
 *
 * This is Dab's Mod's way and its code (gebeance.c, the hdiffpatch glue;
 * dabs-mod aae28c8, MIT; port/licenses/dabs-mod.txt): what comes out is an
 * overlay, not a second release. The files the patch writes, changes or
 * renames go in ce/files/ beside the imported files/, laid out the same, and
 * the Bean loaders read it first (gevrBeanCeRoot()); the rest stay as they
 * are. Two differences from Dab's Mod: the whole overlay is read, not only its
 * art, since the port has not rebuilt the Community Edition's fixes into its
 * own source as Dab's Mod has; and the executable's patch is not applied,
 * since the import keeps no default.xex. Its fog, the one thing taken from
 * it, is built in (gevr_bean_fog.h, used while communityEdition() sees the
 * overlay).
 *
 * The patch checks its own checksums of every file it reads, which is the
 * updater's own rule: it applies to an unmodified "Fyodorovna" release and to
 * nothing else. A copy it does not fit is said so, and nothing is kept.
 */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _FILE_OFFSET_BITS 64

#include "gevr_bean_ce.h"
#include "gevr_bean_import.h"
#include "gevr_bean_log.h"
#include "../external/hdiffpatch/hdpglue.h"

#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int VrBeanCommunityEdition;

// the imported copy as the game sees it (gevr_bean.c's first root, one up)
#define CE_GAME_DIR "../texture-packs/ge007-bean/ce"
// the entry of the updater's zip that is applied
#define CE_DIFF_ENTRY "CEUpdate/files.diff"
// written last, once the overlay is whole: the size of the zip it came from
#define CE_MARKER "applied"

#define CE_MAXPATH 1024

/* The marker's state, looked up once: Bean's fog asks for the overlay on every
 * room and prop list it draws (gevr_bean_world.c communityEdition()), and a
 * stat() each time was an eighth of the render thread in Jungle. Only
 * gevrBeanCeApply() writes or removes the marker, and it forgets this. */
static int s_applied = -1;

int gevrBeanCeApplied(void)
{
	struct stat st;

	if (s_applied < 0) {
		s_applied = stat(CE_GAME_DIR "/" CE_MARKER, &st) == 0;
	}
	return s_applied;
}

const char *gevrBeanCeRoot(void)
{
	return VrBeanCommunityEdition && gevrBeanCeApplied() ? CE_GAME_DIR "/files" : NULL;
}

static void failMsg(char *msg, size_t msgLen, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, msgLen, fmt, ap);
	va_end(ap);
	GEVR_BEAN_LOG("bean ce: %s", msg);
}

static int removeOne(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;
	remove(path);
	return 0;
}

static void removeTree(const char *path)
{
	struct stat st;

	if (lstat(path, &st) == 0) {
		nftw(path, removeOne, 16, FTW_DEPTH | FTW_PHYS);
	}
}

// The levels' collision meshes, which the import leaves behind too: the game
// has its own.
static int removeHits(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;

	if (strstr(path, "_hits/") || (strlen(path) > 5 && !strcmp(path + strlen(path) - 5, "_hits"))) {
		remove(path);
	}

	return 0;
}

struct cerefs {
	const char *files;
	int count;
	int missing;
};

static void noteRef(const char *name, void *arg)
{
	struct cerefs *refs = arg;
	char path[CE_MAXPATH];
	struct stat st;

	refs->count++;
	snprintf(path, sizeof(path), "%s%s", refs->files, name);

	if (stat(path, &st) != 0) {
		if (refs->missing++ < 8) {
			GEVR_BEAN_LOG("bean ce: the copy has no files/%s, which the update needs", name);
		}
	}
}

static int applyUpdate(FILE *zip, const char *beanDir, char *msg, size_t msgLen);

int gevrBeanCeApply(FILE *zip, const char *beanDir, char *msg, size_t msgLen)
{
	const int written = applyUpdate(zip, beanDir, msg, msgLen);

	s_applied = -1;   // the marker was removed, and maybe written again
	return written;
}

static int applyUpdate(FILE *zip, const char *beanDir, char *msg, size_t msgLen)
{
	char dir[CE_MAXPATH];
	char diff[CE_MAXPATH];
	char oldDir[CE_MAXPATH];
	char outDir[CE_MAXPATH];
	char done[CE_MAXPATH];
	char marker[CE_MAXPATH];
	struct cerefs refs;
	struct stat st;
	long long zipSize = -1;
	int written;
	FILE *fp;

	msg[0] = '\0';

	if (zip && fstat(fileno(zip), &st) == 0) {
		zipSize = (long long)st.st_size;
	}

	snprintf(oldDir, sizeof(oldDir), "%s/files/", beanDir);
	snprintf(dir, sizeof(dir), "%s/ce", beanDir);
	snprintf(diff, sizeof(diff), "%s/files.diff", dir);
	snprintf(outDir, sizeof(outDir), "%s/files.new/", dir);
	snprintf(done, sizeof(done), "%s/files", dir);
	snprintf(marker, sizeof(marker), "%s/" CE_MARKER, dir);

	if (stat(oldDir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		if (zip) {
			fclose(zip);
		}

		failMsg(msg, msgLen, "Choose your GoldenEye XBLA copy first.");
		return -1;
	}

	// what an earlier try left
	mkdir(dir, 0755);
	removeTree(outDir);
	remove(diff);

	if (gevrBeanImportZipEntry(zip, CE_DIFF_ENTRY, diff, msg, msgLen) != 0) {
		if (strstr(msg, "has no")) {
			failMsg(msg, msgLen, "That is not the Community Edition updater: it has no %s.", CE_DIFF_ENTRY);
		}

		remove(diff);
		return -1;
	}

	// The files it patches from. A copy imported before the import kept them
	// all has only Bean's own.
	memset(&refs, 0, sizeof(refs));
	refs.files = oldDir;

	if (!hdpListOldRefs(diff, noteRef, &refs)) {
		failMsg(msg, msgLen, "Could not read the update in that zip: is it CommunityEditionUpdaterV6.zip?");
		remove(diff);
		return -1;
	}

	if (refs.missing > 0) {
		failMsg(msg, msgLen, "Choose your GoldenEye XBLA copy again first: this one lacks %d files the update needs.",
				refs.missing);
		remove(diff);
		return -1;
	}

	mkdir(outDir, 0755);
	written = hdpApplyOverlay(oldDir, diff, outDir);
	remove(diff);

	if (written <= 0) {
		removeTree(outDir);
		failMsg(msg, msgLen, "The update does not fit your copy (step %d): it applies only to the unmodified GoldenEye XBLA release.",
				written);
		return -1;
	}

	nftw(outDir, removeHits, 16, FTW_DEPTH | FTW_PHYS);

	// the old overlay goes only once the new one is whole
	remove(marker);
	removeTree(done);

	// (rename wants no trailing separator)
	outDir[strlen(outDir) - 1] = '\0';

	if (rename(outDir, done) != 0) {
		removeTree(outDir);
		failMsg(msg, msgLen, "Could not finish the update in %s.", dir);
		return -1;
	}

	fp = fopen(marker, "wb");

	if (!fp || fprintf(fp, "%lld\n", zipSize) < 0 || fclose(fp) != 0) {
		failMsg(msg, msgLen, "Could not write %s: is the headset full?", marker);
		return -1;
	}

	GEVR_BEAN_LOG("bean ce: applied, %d files from %d references (zip %lld bytes)", written, refs.count, zipSize);

	return written;
}

#ifdef __ANDROID__
#include <jni.h>

/*
 * ModManager's Community Edition job: fd is the picked updater zip, opened by
 * Java and handed over (detachFd); beanDir the imported pack's folder. "" when
 * it worked, else the reason to show.
 */
JNIEXPORT jstring JNICALL Java_com_gevr_port_ModManager_nativeBeanCeApply(JNIEnv *env, jclass cls, jint fd, jstring beanDir)
{
	char msg[256];
	const char *d = (*env)->GetStringUTFChars(env, beanDir, NULL);
	FILE *src = fdopen(fd, "rb");
	(void)cls;

	if (!src) {
		close(fd);
	}

	const int n = gevrBeanCeApply(src, d, msg, sizeof(msg));

	(*env)->ReleaseStringUTFChars(env, beanDir, d);

	return (*env)->NewStringUTF(env, n < 0 ? msg : "");
}
#endif
