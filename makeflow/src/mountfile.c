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
#include <time.h>
#include <unistd.h>

#include "copy_tree.h"
#include "copy_stream.h"
#include "create_dir.h"
#include "debug.h"
#include "http_query.h"
#include "list.h"
#include "makeflow_log.h"
#include "md5.h"
#include "mkdir_recursive.h"
#include "mountfile.h"
#include "path.h"
#include "unlink_recursive.h"
#include "xxmalloc.h"

#define HTTP_TIMEOUT 300

/* create_link creates a link from link_name to link_target.
 * first try to create a hard link, then try to create a symlink when failed to create a hard link.
 * return 0 on success, non-zero on failure.
 */
int create_link(const char *link_target, const char *link_name) {
	if(link(link_target, link_name)) {
		LDEBUG("link(%s, %s) failed: %s!\n", link_target, link_name, strerror(errno));
		if(symlink(link_target, link_name)) {
			LDEBUG("symlink(%s, %s) failed: %s!\n", link_target, link_name, strerror(errno));
			return -1;
		}
	}
	return 0;
}

/* mount_install_http downloads a dependency from source to cache_path.
 * @param source: a http url.
 * @param cache_path: a file path in the cache dir.
 * return 0 on success; return -1 on failure.
 */
int mount_install_http(const char *source, const char *cache_path) {
	if(http_fetch_to_file(source, cache_path, time(NULL) + HTTP_TIMEOUT) < 0) {
		LDEBUG("http_fetch_to_file(%s, %s, ...) failed!\n", source, cache_path);
		return -1;
	}
	return 0;
}

/* mount_check_http checks whether a http url is available by sending a HEAD request to it.
 * return 0 on success; return -1 on failure.
 */
int mount_check_http(const char *url) {
	struct link *link = http_query(url, "HEAD", time(0) + HTTP_TIMEOUT);
	if(!link) {
		LDEBUG("http_query(%s, \"HEAD\", ...) failed!\n", url);
		fprintf(stderr, "http_query(%s, \"HEAD\", ...) failed!\n", url);
		return -1;
	}
	return 0;
}

/* mount_install_local copies source to target.
 * @param source: a local file path which must exist already.
 * @param target: a local file path which must not exist.
 * @param cache_path: a file path in the cache dir.
 * @param s_type: the file type of source.
 * return 0 on success; return -1 on failure.
 */
int mount_install_local(const char *source, const char *target, const char *cache_path, file_type s_type) {
	switch(s_type) {
	case FILE_TYPE_REG:
		if(copy_file_to_file(source, cache_path) < 0) {
			LDEBUG("copy_file_to_file from %s to %s failed.\n", source, cache_path);
			return -1;
		}
		break;
	case FILE_TYPE_LNK:
		if(copy_symlink(source, cache_path)) {
			LDEBUG("copy_symlink from %s to %s failed.\n", source, cache_path);
			return -1;
		}
	case FILE_TYPE_DIR:
		if(copy_dir(source, cache_path)) {
			LDEBUG("copy_dir from %s to %s failed.\n", source, cache_path);
			return -1;
		}
		break;
	default:
		break;
	}

	return 0;
}

/* mount_check checks the validity of source and target.
 * It also sets the file type when source is a local path.
 * @param source: the source location of a dependency, which can be a local file path or http URL.
 * @param target: a local file path which must not exist.
 * @param s_type: the file type of source
 * return 0 on success; return -1 on failure.
 */
int mount_check(const char *source, const char *target, file_type *s_type) {
	if(!source || !*source) {
		LDEBUG("the source (%s) can not be empty!\n", source);
		fprintf(stderr, "the source (%s) can not be empty!\n", source);
		return -1;
	}

	if(!target || !*target) {
		LDEBUG("the target (%s) can not be empty!\n", target);
		fprintf(stderr, "the target (%s) can not be empty!\n", target);
		return -1;
	}

	/* Check whether the target is an absolute path. */
	if(target[0] == '/') {
		LDEBUG("the target (%s) should not be an absolute path!\n", target);
		fprintf(stderr, "the target (%s) should not be an absolute path!\n", target);
		return -1;
	}

	/* check whether target includes .. */
	if(path_has_doubledots(target)) {
		LDEBUG("the target (%s) include ..!\n", target);
		fprintf(stderr, "the target (%s) include ..!\n", target);
		return -1;
	}

	/* check whether target includes any symlink link, this check prevent the makeflow breaks out the CWD. */
	if(path_has_symlink(target)) {
		LDEBUG("the target (%s) should not include any symbolic link!\n", target);
		fprintf(stderr, "the target (%s) should not include any symbolic link!\n", target);
		return -1;
	}

	/* Check whether target already exists. */
	if(!access(target, F_OK)) {
		LDEBUG("the target (%s) already exists!\n", target);
		fprintf(stderr, "the target (%s) already exists!\n", target);
		return -1;
	}

	if(!strncmp(source, "http://", 7)) {
		return mount_check_http(source);
	} else {
		/* Check whether source already exists. */
		if(access(source, F_OK)) {
			LDEBUG("the source (%s) does not exist!\n", source);
			fprintf(stderr, "the source (%s) does not exist!\n", source);
			return -1;
		}

		/* check whether source is REG, LNK, DIR */
		if((*s_type = check_file_type(source)) == FILE_TYPE_UNSUPPORTED) {
			fprintf(stderr, "source should be regular file, link, or dir!\n");
			return -1;
		}

		/* check whether source is an ancestor directory of target */
		if(is_subdir(source, target)) {
			LDEBUG("source (%s) is an ancestor of target (%s), and can not be copied into target!\n", source, target);
			fprintf(stderr, "source (%s) is an ancestor of target (%s), and can not be copied into target!\n", source, target);
			return -1;
		}
	}

	return 0;
}

/* md5_cal_source calculates the checksum of a file path.
 * @param source: the source location of a dependency, which can be a local file path or http URL.
 * @is_local: whether source is a lcoal path.
 */
char *md5_cal_source(const char *source, int is_local) {
	char *cache_name = NULL;

	if(is_local) {
		char *s_real = NULL;

		/* for local path, calculate the checksum of its realpath */
		s_real = realpath(source, NULL);
		if(!s_real) {
			LDEBUG("realpath(`%s`) failed: %s!\n", source, strerror(errno));
			return NULL;
		}

		cache_name = md5_cal(s_real);

		if(!cache_name) {
			LDEBUG("md5_cal(%s) failed: %s!\n", s_real, strerror(errno));
		}
		free(s_real);
	} else {
		cache_name = md5_cal(source);
	}
	return cache_name;
}

/* amend_cache_path adds ../ in front of cache_path.
 * @param cache_path: a file path.
 * @param depth: how many `../` should be added.
 */
char *amend_cache_path(char *cache_path, int depth) {
	char *p = NULL, *t = NULL;
	char *link_cache_path = NULL;
	int i;

	if(depth <= 0) return cache_path;

	/* calculate how many ../ should be attached to cache_path. */
	p = malloc(sizeof(char) * depth*3);
	if(!p) {
		LDEBUG("malloc failed: %s!\n", strerror(errno));
		return NULL;
	}

	t = p;
	for(i=0; i<depth; i++) {
		*t++ = '.';
		*t++ = '.';
		*t++ = '/';
	}
	*(--t) = '\0';

	link_cache_path = path_concat(p, cache_path);
	if(!link_cache_path) {
		LDEBUG("malloc failed: %s!\n", strerror(errno));
	}

	free(p);
	return link_cache_path;
}


/* mount_install copies source to target.
 * @param source: the source location of a dependency, which can be a local file path or http URL.
 * @param target: a local file path which must not exist.
 * @param cache_dir: the dirname of the cache used to store all the dependencies specified in a mountfile.
 * @param df: a dag_file structure
 * @param type: the mount type
 * return 0 on success; return -1 on failure.
 */
int mount_install(const char *source, const char *target, const char *cache_dir, struct dag_file *df, source_type *type) {
	char *cache_name = NULL;;
	char *cache_path = NULL;
	char *dirpath = NULL, *p = NULL;
	int depth;
	file_type s_type;

	/* check the validity of source and target */
	if(mount_check(source, target, &s_type)) {
		LDEBUG("mount_check(%s, %s) failed: %s!\n", source, target, strerror(errno));
		return -1;
	}

	/* set up the type of the source: http or local */
	if(!strncmp(source, "http://", 7)) {
		*type = SOURCE_HTTP;
	} else {
		*type = SOURCE_LOCAL;
	}

	/* calculate the filename in the cache dir */
	cache_name = md5_cal_source(source, *type == SOURCE_LOCAL);
	if(!cache_name) {
		LDEBUG("md5_cal_source(%s) failed: %s!\n", source, strerror(errno));
		return -1;
	}

	cache_path = path_concat(cache_dir, cache_name);
	if(!cache_path) {
		free(cache_name);
		return -1;
	}

	/* if cache_path does not exist, copy it from source to cache_path. */
	if(access(cache_path, F_OK)) {
		int r = 0;
		if(*type == SOURCE_HTTP) {
			r = mount_install_http(source, cache_path);
		} else {
			r = mount_install_local(source, target, cache_path, s_type);
		}

		if(r) {
			free(cache_name);
			free(cache_path);
			return r;
		}
	}

	if(df->cache_name) free(df->cache_name);
	df->cache_name = cache_name;

	/* calculate the depth of target relative to CWD. For example, if target = "a/b/c", path_depth returns 3. */
	depth = path_depth(target);
	if(depth < 1) {
		LDEBUG("path_depth(%s) failed!\n", target);
		return -1;
	}
	LDEBUG("path_depth(%s) = %d!\n", target, depth);

	/* Create the parent directories for target.
	 * If target is "dir1/dir2/file", then create dir1 and dir2 using `mkdir -p dir1/dir2`.
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

	/* link target to the file in the cache dir */
	if(depth == 1) {
		if(create_link(cache_path, target)) {
			LDEBUG("create_link(%s, %s) failed!\n", cache_path, target);
			free(cache_path);
			return -1;
		}
		free(cache_path);
		return 0;
	}

	/* construct the link_target of target */
	if(link(cache_path, target)) {
		char *link_cache_path = NULL;

		LDEBUG("link(%s, %s) failed: %s!\n", cache_path, target, strerror(errno));

		/* link_cache_path must not equals to cache_path, because depth here is > 1. */
		link_cache_path = amend_cache_path(cache_path, depth-1);
		if(!link_cache_path) {
			LDEBUG("amend_cache_path(%s, %d) failed: %s!\n", cache_path, depth, strerror(errno));
			free(cache_path);
			return -1;
		}
		free(cache_path);

		if(create_link(link_cache_path, target)) {
			LDEBUG("create_link(%s, %s) failed!\n", link_cache_path, target);
			free(link_cache_path);
			return -1;
		}
		free(link_cache_path);
		return 0;
	} else {
		free(cache_path);
		return 0;
	}
}

/* mount_uninstall removes the target.
 * return 0 on success, -1 on failure.
 */
int mount_uninstall(const char *target) {
	file_type t_type;

	if(!target || !*target) return 0;

	/* Check whether target already exists. */
	if(access(target, F_OK)) {
		LDEBUG("the target (%s) does not exist!\n", target);
		return 0;
	}

	/* Check whether the target is an absolute path. */
	if(target[0] == '/') {
		LDEBUG("the target (%s) should not be an absolute path!\n", target);
		fprintf(stderr, "the target (%s) should not be an absolute path!\n", target);
		return -1;
	}

	/* check whether target includes .. */
	if(path_has_doubledots(target)) {
		LDEBUG("the target (%s) include ..!\n", target);
		fprintf(stderr, "the target (%s) include ..!\n", target);
		return -1;
	}

	/* check whether target is REG, LNK, DIR */
	if((t_type = check_file_type(target)) == FILE_TYPE_UNSUPPORTED)
		return -1;

	if(unlink_recursive(target)) {
		LDEBUG("Fails to remove %s!\n", target);
		fprintf(stderr, "Fails to remove %s!\n", target);
		return -1;
	}

	return 0;
}

int mountfile_parse(const char *mountfile, struct dag *d) {
	FILE *f;
	char line[PATH_MAX*2 + 1]; /* each line of the mountfile includes the target path, a space and the source path. */
	size_t lineno = 0;
	int err_num = 0;

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
		char *p;
		struct dag_file *df;
		file_type s_type;

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

		/* set up dag_file->source */
		df = dag_file_from_name(d, target);
		if(!df) {
			debug(D_MAKEFLOW, "%s is not in the dag_file list", target);
			continue;
		}

		if(mount_check(source, target, &s_type)) {
			LDEBUG("mount_check(%s, %s) failed: %s!\n", source, target, strerror(errno));
			err_num++;
			continue;
		}

		p = xxstrdup(source);
		if(!p) {
			LDEBUG("xxstrdup(%s) failed!\n", source);
			err_num++;
		}

		/* df->source may already be set based on the information from the makeflow log file, so free it first. */
		if(df->source) free(df->source);

		df->source = p;
	}

	if(fclose(f)) {
		LDEBUG("fclose(`%s`) failed: %s!\n", mountfile, strerror(errno));
		return -1;
	}

	if(err_num) return -1;

	return 0;
}

/* check_cache_dir checks the validity of the cache dir, and create it if possible.
 * @param cache: a file path.
 * return 0 on success, return non-zero on failure.
 */
int check_cache_dir(const char *cache) {
	struct stat st;

	/* Checking principle: the cache must locate under the CWD. */
	if(!cache || !*cache) {
		LDEBUG("the cache (%s) can not be empty!\n", cache);
		fprintf(stderr, "the cache (%s) can not be empty!\n", cache);
		return -1;
	}

	/* Check whether the cache is an absolute path. */
	if(cache[0] == '/') {
		LDEBUG("the cache (%s) should not be an absolute path!\n", cache);
		fprintf(stderr, "the cache (%s) should not be an absolute path!\n", cache);
		return -1;
	}

	/* check whether cache includes .. */
	if(path_has_doubledots(cache)) {
		LDEBUG("the cache (%s) include ..!\n", cache);
		fprintf(stderr, "the cache (%s) include ..!\n", cache);
		return -1;
	}

	/* check whether cache includes any symlink link, this check prevent the makeflow breaks out the CWD. */
	if(path_has_symlink(cache)) {
		LDEBUG("the cache (%s) should not include any symbolic link!\n", cache);
		fprintf(stderr, "the cache (%s) should not include any symbolic link!\n", cache);
		return -1;
	}

	/* Check whether cache already exists. */
	if(access(cache, F_OK)) {
		mode_t default_dirmode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		if(errno != ENOENT) {
			LDEBUG("access(%s) failed: %s\n", cache, strerror(errno));
			return -1;
		}

		LDEBUG("the cache (%s) does not exist, creating it ...\n", cache);
		if(mkdir_recursive(cache, default_dirmode)) {
			LDEBUG("mkdir_recursive(%s) failed: %s\n", cache, strerror(errno));
			return -1;
		}
		return 0;
	}

	/* check whether cache is a dir */
	if(lstat(cache, &st) == 0) {
		if(!S_ISDIR(st.st_mode)) {
			LDEBUG("the cache (%s) should be a dir!\n", cache);
			fprintf(stderr, "the cache (%s) should be a dir!\n", cache);
			return -1;
		}
	} else {
		LDEBUG("lstat(%s) failed: %s!\n", cache, strerror(errno));
		fprintf(stderr, "lstat(%s) failed: %s!\n", cache, strerror(errno));
		return -1;
	}

	return 0;
}

int mount_install_all(struct dag *d) {
	struct list *list;
	struct dag_file *df;
	char *cache_dir;

	if(!d) return 0;

	if(d->cache_dir) {
		/* check the validity of the cache_dir and create it if neccessary and feasible. */
		if(check_cache_dir(d->cache_dir)) {
			return -1;
		}

	} else {
		/* Create a unique cache dir */
		cache_dir = xxstrdup(".makeflow_cache.XXXXXX");
		if(!cache_dir) {
			LDEBUG("xxstrdup failed: %s\n", strerror(errno));
			return -1;
		}

		if(mkdtemp(cache_dir) == NULL) {
			LDEBUG("mkdtemp(%s) failed: %s\n", cache_dir, strerror(errno));
			free(cache_dir);
			return -1;
		}

		d->cache_dir = cache_dir;
	}

	/* log the cache dir info */
	makeflow_log_cache_event(d, d->cache_dir);

	list = dag_input_files(d);
	if(!list) return 0;

	list_first_item(list);
	while((df = (struct dag_file *)list_next_item(list))) {
		source_type type;
		if(!df->source)
			continue;

		if(mount_install(df->source, df->filename, d->cache_dir, df, &type)) {
			list_delete(list);
			return -1;
		}

		/* log the dependency */
		makeflow_log_mount_event(d, df->filename, df->source, df->cache_name, type);
	}
	list_delete(list);
	return 0;
}

int mount_uninstall_all(struct dag *d) {
	struct list *list;
	struct dag_file *df;
	int r = 0;
	if(!d) return 0;

	list = dag_input_files(d);
	if(!list) {
		return mount_uninstall(d->cache_dir);
	}

	list_first_item(list);
	while((df = (struct dag_file *)list_next_item(list))) {
		if(!df->source || !df->cache_name) {
			continue;
		}

		/* mount_uninstall_all tries to remove as much as possible and does not stop when any error is encountered. */
		if(mount_uninstall(df->filename)) r = -1;
	}
	list_delete(list);

	/* remove the cache dir */
	if(r == 0) r = mount_uninstall(d->cache_dir);

	return r;
}

/* vim: set noexpandtab tabstop=4: */
