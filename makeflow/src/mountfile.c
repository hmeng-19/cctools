/*
Copyright (C) 2008- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "copy_tree.h"
#include "copy_stream.h"
#include "create_dir.h"
#include "debug.h"
#include "mountfile.h"
#include "path.h"
#include "unlink_recursive.h"
#include "xxmalloc.h"

/* mount_install copies source to target.
 * To allow the makeflow cleanup to be certain which files are created by the introduction of the mountfile, target should not exist.
 * return 0 on success; return -1 on failure.
 */
int mount_install(const char *source, const char *target) {
	char *dirpath, *p;
	file_type s_type;

	/* check whether source is REG, LNK, DIR */
	if((s_type = check_file_type(source)) == FILE_TYPE_UNSUPPORTED)
		return -1;

	/* Check whether the target is an absolute path. */
	if(target[0] == '/') {
		LDEBUG("the target (%s) should not be an absolute path!\n", target);
		return -1;
	}

	/* check whether target includes .. */
	if(path_has_doubledots(target)) {
		LDEBUG("the target (%s) include ..!\n", target);
		return -1;
	}

	/* Check whether target already exists. */
	if(!access(target, F_OK)) {
		LDEBUG("the target (%s) already exists!\n", target);
		return -1;
	}

	/* check whether source is an ancestor directory of target */
	if(is_subdir(source, target)) {
		LDEBUG("source (%s) is an ancestor of target (%s), and can not be copied into target!\n", source, target);
		return -1;
	}

	/* Create the parent directories for target.
	 * If target is "dir1/dir2/file", then create dir1 and dir2 using `makedir -p dir1/dir2`.
	 */
	p = xxstrdup(target);
	if(!p) return -1;

	dirpath = dirname(p); /* Please do NOT free dirpath, free p instead. */
	if(access(dirpath, F_OK) && !create_dir(dirpath, 0755)) {
		free(p);
		LDEBUG("failed to create the parent directories of the target (%s)!\n", target);
		return -1;
	}
	free(p);

	switch(s_type) {
	case FILE_TYPE_REG:
		if(copy_file_to_file(source, target) < 0) {
			LDEBUG("copy_file_to_file from %s to %s failed.\n", source, target);
			return -1;
		}
		break;
	case FILE_TYPE_LNK:
		if(copy_symlink(source, target)) {
			LDEBUG("copy_symlink from %s to %s failed.\n", source, target);
			return -1;
		}
	case FILE_TYPE_DIR:
		if(copy_dir(source, target)) {
			LDEBUG("copy_dir from %s to %s failed.\n", source, target);
			return -1;
		}
		break;
	default:
		break;
	}

	return 0;
}

/* remove the target.
 * return 0 on success, -1 on failure.
 */
int mount_uninstall(const char *target) {
	file_type t_type;

	/* Check whether target already exists. */
	if(access(target, F_OK)) {
		LDEBUG("the target (%s) does not exist!\n", target);
		return 0;
	}

	/* Check whether the target is an absolute path. */
	if(target[0] == '/') {
		LDEBUG("the target (%s) should not be an absolute path!\n", target);
		return -1;
	}

	/* check whether target includes .. */
	if(path_has_doubledots(target)) {
		LDEBUG("the target (%s) include ..!\n", target);
		return -1;
	}

	/* check whether target is REG, LNK, DIR */
	if((t_type = check_file_type(target)) == FILE_TYPE_UNSUPPORTED)
		return -1;

	if(unlink_recursive(target)) {
		LDEBUG("Fails to remove %s!\n", target);
		return -1;
	}

	return 0;
}

int mountfile_parse(const char *mountfile, int is_install) {
	FILE *f;
	char line[PATH_MAX*2 + 1]; /* each line of the mountfile includes the target path, a space and the source path. */
	size_t lineno = 0;

	debug(D_MAKEFLOW, "The --mounts option: %s\n", mountfile);

	if(access(mountfile, F_OK)) {
		LDEBUG("the mountfile (%s) does not exist!\n", mountfile);
		return -1;
	}

	f = fopen(mountfile, "r");
	if(!f) {
		LDEBUG("couldn't open mountfile (%s): %s\n", mountfile, strerror(errno));
		return -1;
	}

	while(fgets(line, sizeof(line), f)) {
		char target[PATH_MAX], source[PATH_MAX];

		lineno++;

		if(line[0] == '#') {
			debug(D_MAKEFLOW, "line %zu is a comment: %s", lineno, line);
			continue;
		}

		debug(D_MAKEFLOW, "Processing line %zu of the mountfile: %s", lineno, line);
		if(sscanf(line, "%s %s", target, source) != 2) {
			LDEBUG("The %zuth line of the mountfile (%s) has an error! The correct format is: <target> <source>\n", lineno, mountfile);
			fclose(f);
			return -1;
		}

		path_remove_trailing_slashes(source);
		path_remove_trailing_slashes(target);

		if(is_install) {
			if(mount_install(source, target)) {
				if(fclose(f)) {
					LDEBUG("fclose(`%s`) failed: %s!\n", mountfile, strerror(errno));
				}
				return -1;
			}
		} else {
			/* mount_uninstall does not care whether source exists or not. */
			if(mount_uninstall(target)) {
				if(fclose(f)) {
					LDEBUG("fclose(`%s`) failed: %s!\n", mountfile, strerror(errno));
				}
				return -1;
			}
		}
	}

	if(fclose(f)) {
		LDEBUG("fclose(`%s`) failed: %s!\n", mountfile, strerror(errno));
		return -1;
	}
	return 0;
}

/* vim: set noexpandtab tabstop=4: */
