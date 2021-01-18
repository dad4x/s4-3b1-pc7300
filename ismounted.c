/*
 * ismounted.c --- check if a device is mounted.
 * Linux specific.
 *
 * Arnold Robbins
 * arnold@skeeve.com
 * January, 2021.
 */

#include <stdio.h>
#include <mntent.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>

/* is_underlying_file --- check if the underlying file is the same as given */

static bool
is_underlying_file(const struct stat *statbuf, const char *loopdev)
{
	char loopfile[BUFSIZ];
	char backing_file[BUFSIZ];
	FILE *fp;
	struct stat sbuf;

	sprintf(loopfile, "/sys/class/block/%s/loop/backing_file", loopdev);
	if ((fp = fopen(loopfile, "r")) == NULL)
		return false;

	if (fgets(backing_file, sizeof(backing_file), fp) == NULL) {
		fclose(fp);
		return false;
	}
	fclose(fp);

	backing_file[strlen(backing_file)-1] = '\0';	// clobber newline

	if (stat(backing_file, &sbuf) < 0) {
		return false;
	}

	bool result = (statbuf->st_dev == sbuf.st_dev
			&& statbuf->st_ino == sbuf.st_ino);

	return result;
}

/* ismounted --- check if device is mounted */

bool
ismounted(const char *device)
{
	FILE *fp = setmntent("/etc/mtab", "r");
	struct mntent *entry;
	struct stat statbuf;

	if (device == NULL || *device == '\0' || stat(device, & statbuf) < 0)
		return false;

	while ((entry = getmntent(fp)) != NULL) {
		if (entry->mnt_fsname[0] != '/') {	// synthetic file system
			continue;
		}

		if (strcmp(device, entry->mnt_fsname) == 0) {
			(void) endmntent(fp);
			return true;
		}

		if (strncmp(entry->mnt_fsname, "/dev/loop", 9) != 0) {
			continue;
		}

		if (is_underlying_file(& statbuf, entry->mnt_fsname + 5)) {
			(void) endmntent(fp);
			return true;
		}
	}

	(void) endmntent(fp);
	return false;
}

#if 0
int main(int argc, char **argv)
{
	for (argc--, argv++; *argv; argc--, argv++) {
		printf("%s is %smounted\n", *argv,
				ismounted(*argv) ? "" : "not ");
	}

	return 0;
}
#endif
