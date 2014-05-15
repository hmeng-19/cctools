#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <utime.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <time.h>
#include <copy_stream.h>

#define SIZE 256

int LINE_MAX=1024;
const char *namelist;
const char *packagepath;
const char *envpath;
int line_num;

int line_process(const char *path, char *caller, int ignore_direntry, int is_direntry);

//files from these paths will be ignored.
const char *special_path[] = {"var", "sys", "dev", "proc", "net", "misc", "selinux"};
#define special_path_len sizeof(special_path)/sizeof(const char *)

//these system calls will result in the whole copy of one file item.
const char *special_caller[] = {"lstat", "stat", "open_object", "bind32", "connect32", "bind64", "connect64", "truncate link1", "mkalloc", "lsalloc", "whoami", "md5", "copyfile1", "copyfile2", "follow_symlink", "link2", "symlink2", "readlink", "unlink"};
#define special_caller_len sizeof(special_caller)/sizeof(const char *)

enum {
	LONG_OPT_NAMELIST = 1,
	LONG_OPT_ENVPATH,
	LONG_OPT_PACKAGEPATH,
};

static void show_help(const char *cmd)
{
	fprintf(stdout, "Use: %s [options] ...\n", cmd);
	fprintf(stdout, " %-34s The path of the namelist list.\n", "   --name-list=<listpath>");
	fprintf(stdout, " %-34s The path of the environment variable file.\n", "   --env-path=<envpath>");
	fprintf(stdout, " %-34s The path of the package.\n", "   --package-path=<packagepath>");
	fprintf(stdout, " %-34s Show the help info.\n", "-h,--help");
	return;
}

void print_time()
{
	time_t curtime;
	struct tm *loctime;
	curtime = time(NULL);
	loctime = localtime(&curtime);
	fputs(asctime(loctime), stdout);
}

/* Compare the strings. */
static int compare(const void * a, const void * b)
{
	/* The pointers point to offsets into "namelist_array", so we need to dereference them to get at the strings. */
	return strcmp(*(const char **) a, *(const char **) b);
}

/* obtain the line number of one file. */
int line_number(const char *filename)
{
	FILE *namelist_file;
	int count;
	char line[LINE_MAX];
	namelist_file = fopen(filename, "r");
	if(!namelist_file) {
		fprintf(stdout, "line_number Can not open sorted namelist file: `%s`", filename);
		exit(1);
	}
	count = 0;
	while(fgets(line, LINE_MAX, namelist_file) != NULL) {
		count++;
	}
	if(namelist_file)
		fclose(namelist_file);
	return count;
}

int sort_uniq_namelist(const char *filename, char sorted_filename[LINE_MAX]) {
	sprintf(sorted_filename, "%s.sort", filename);
	char command[LINE_MAX*3];
	sprintf(command, "sort -u %s > %s", filename, sorted_filename);
	system(command);
	return 0;
}

void relative_path(char *newpath, const char *oldpath, const char *path)
{
	char *s;
	strcpy(newpath, "");
	s = strchr(path, '/');
	while(s != NULL) {
		s++;
		strcat(newpath, "../");
		s = strchr(s, '/');
	}
	newpath[strlen(newpath) - 4] = '\0';
	strcat(newpath, oldpath);
}

void remove_final_slashes(char *path)
{
	int n;
	n = strlen(path);
	while(path[n-1] == '/') {
		n--;
	}
	path[n] = '\0';
}

int initialize_namelist_array(char namelist_array[line_num][LINE_MAX], char *filename) {
	int i;
	FILE *namelist_file = fopen(filename, "r");
	if(!namelist_file) {
		fprintf(stdout, "initialize_namelist_array Can not open sorted namelist file: `%s`", filename);
		exit(1);
	}
	char line[LINE_MAX];
	for (i = 0; fgets(line, LINE_MAX, namelist_file); i++) {
		strcpy(namelist_array[i], line);
	}
	if(namelist_file)
		fclose(namelist_file);
	return 0;
}

//sort all the lines of the namelist file.
void sort_namelist(char namelist_array[line_num][LINE_MAX]) {
	int i;
	FILE *namelist_file = fopen(namelist, "r");
	if(!namelist_file) {
		fprintf(stdout, "Can not open namelist file: `%s`", namelist);
		exit(1);
	}
	char line[LINE_MAX];
	for (i = 0; fgets(line, LINE_MAX, namelist_file); i++) {
		strcpy(namelist_array[i], line);
	}
	qsort(namelist_array, line_num, sizeof(const char *), compare);
	if(namelist_file)
		fclose(namelist_file);
}

/* 
Function with behaviour like `mkdir -p'.  
Correctly copying the file permissions from AFS items into the package, 
which is stored on the local filesystem, is inefficient, because AFS has
its own ACLs, which is different UNIX file permission mechanism.
*/
int mkpath(const char *path, mode_t mode, int fixed_mode) {
	printf("mkpath: %s\n", path);
	if(access(path, F_OK) == 0) {
		printf("%s already exists, mkpath exist!\n", path);
		return 0;
	}

	if(fixed_mode == 0) {
		const char *old_path;
		old_path = path + strlen(packagepath);
		struct stat st;
		if(stat(old_path, &st) == 0) {
			mode = st.st_mode;
		} else {
			fprintf(stdout, "stat(`%s`) fails: %s\n", old_path, strerror(errno));
			return -1;
		}
	}

	char pathcopy[LINE_MAX], *parent_dir;
	int rv;
	rv = -1;
	if(strcmp(path, ".") == 0 || strcmp(path, "/") == 0)
		return 0;

	if(strcpy(pathcopy, path) == NULL)
		exit(1);

	if((parent_dir = dirname(pathcopy)) == NULL)
		goto out;

	if((mkpath(parent_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH, 1) == -1) && (errno != EEXIST))
		goto out;

	if((mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) && (errno != EEXIST))
		rv = -1;
	else
		rv = 0;
out:
	return rv;
}


//preprocess: check whether the environment variable file exists; check whether the list namelist file exists; check whether the package path exists;
//sorting the listfile(single func);
int prepare_work()
{
	if(access(envpath, F_OK) == -1) {
		fprintf(stdout, "The environment variable file (%s) does not exist.\n", envpath);
		return -1;
	}
	if(access(namelist, F_OK) == -1) {
		fprintf(stdout, "The namelist file (%s) does not exist.\n", namelist);
		return -1;
	}
	if(access(packagepath, F_OK) != -1) {
		fprintf(stdout, "The package path (%s) has already existed, please delete it first or refer to another package path.\n", packagepath);
		return -1;
	}
	if(mkpath(packagepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH, 1) == -1) {
		fprintf(stdout, "mkdir(%s) fails: %s", packagepath, strerror(errno));
		return -1;
	}
	return 0;
}

int is_special_caller(char *caller)
{
	int i;
	for(i = 0; i < (int) special_caller_len; i++){
		if(strcmp(special_caller[i], caller) == 0) {
			return 1;
		}
	}
	return 0;
}

int is_special_path(const char *path)
{
	int i, size;
	char pathcopy[LINE_MAX], *first_dir, *tmp_dir;
	strcpy(pathcopy, path);
	first_dir = strchr(pathcopy, '/') + 1;
	tmp_dir = strchr(first_dir, '/');
	if(tmp_dir == NULL) {
		size = strlen(first_dir);
	} else {
		size = strlen(first_dir) - strlen(tmp_dir);
	}
	first_dir[size] = '\0';
	for(i = 0; i < (int) special_path_len; i++){
		if(strcmp(special_path[i], first_dir) == 0) {
			return 1;
		}
	}
	return 0;
}

void print_permissions(char * dir_name)
{
	struct stat fileStat;
	if(lstat(dir_name, &fileStat) == 0) {
		printf("File Permissions: %s\t", dir_name);
		printf((S_ISDIR(fileStat.st_mode)) ? "d" : "-");
		printf((fileStat.st_mode & S_IRUSR) ? "r" : "-");
		printf((fileStat.st_mode & S_IWUSR) ? "w" : "-");
		printf((fileStat.st_mode & S_IXUSR) ? "x" : "-");
		printf((fileStat.st_mode & S_IRGRP) ? "r" : "-");
		printf((fileStat.st_mode & S_IWGRP) ? "w" : "-");
		printf((fileStat.st_mode & S_IXGRP) ? "x" : "-");
		printf((fileStat.st_mode & S_IROTH) ? "r" : "-");
		printf((fileStat.st_mode & S_IWOTH) ? "w" : "-");
		printf((fileStat.st_mode & S_IXOTH) ? "x" : "-");
		printf("\n\n");
	} else {
		printf("lstat(`%s`) fails: %s\n", dir_name, strerror(errno));
	}
}

int dir_entry(const char* filename)
{
	struct stat source_stat;
	char new_path[LINE_MAX];
	strcpy(new_path, packagepath);
	strcat(new_path, filename);
	if(access(new_path, F_OK) == 0) {
		fprintf(stdout, "`%s` already exists\n", new_path);
	} else if(lstat(filename, &source_stat) == 0) {
		if(S_ISDIR(source_stat.st_mode)) {
			printf("dir_entry: `%s`, ---dir\n", filename);
			line_process(filename, "metadatacopy", 1, 1);
		} else if(S_ISCHR(source_stat.st_mode)) {
			printf("dir_entry: `%s`, ---character, do nothing!\n", filename);
		} else if(S_ISBLK(source_stat.st_mode)) {
			printf("dir_entry: `%s`, ---block, do nothing!\n", filename);
		} else if(S_ISREG(source_stat.st_mode)) {
			printf("dir_entry: `%s`, ---regular file\n", filename);
			line_process(filename, "metadatacopy", 1, 1);
		} else if(S_ISFIFO(source_stat.st_mode)) {
			printf("dir_entry: `%s`, ---fifo special file, do nothing!\n", filename);
		} else if(S_ISLNK(source_stat.st_mode)) {
			printf("dir_entry: `%s`, ---link file, do nothing!\n", filename);
			line_process(filename, "metadatacopy", 1, 1);
		} else if(S_ISSOCK(source_stat.st_mode)) {
			printf("dir_entry: `%s`, ---socket file, do nothing!\n", filename);
		}
	} else {
		fprintf(stdout, "lstat(`%s`): %s\n", filename, strerror(errno));
	}
	return 0;
}

int create_dir_subitems(const char *path, char *new_path) {
	DIR *dir;
	struct dirent *entry;
	char full_entrypath[LINE_MAX], dir_name[LINE_MAX];
	if(strcpy(dir_name, path) == NULL) {
		printf("create_dir_subitems %s error:%s\n", path, strerror(errno));
		return -1;
	}
	printf("create_dir_subitems: `%s`\n", path);
	strcat(dir_name, "/");
	dir = opendir(path);
	if (dir != NULL)
	{
		while ((entry = readdir(dir)))
		{
			strcpy(full_entrypath, dir_name);
			strcat(full_entrypath, entry->d_name);
			dir_entry(full_entrypath);
		}
		closedir(dir);
	} else {
		printf("Couldn't open the directory `%s`.\n", path);
	}
	return 0;
}

/*
ignore_direntry is to tell whether the directory struture of one directory needs to be maintained. The directory structure here means: create each subitems but not copy their contents.
if is_direntry is 1, ignore the process to check whether its parent dir has been created in the target package, which can greatly reduce the amount of `access` syscall.
*/
int line_process(const char *path, char *caller, int ignore_direntry, int is_direntry)
{
	printf("`%s`\n", path);
	if(is_special_path(path)) {
		fprintf(stdout, "`%s`: Special path, ignore!\n", path);
		return 0;
	}
	int fullcopy = 0;
	ignore_direntry = 1;
	if(strcmp(caller,"metadatacopy") == 0) {
		fullcopy = 0;
	} else if(strcmp(caller,"fullcopy") == 0 || is_special_caller(caller)) {
		fullcopy = 1;
		ignore_direntry = 0;
	}

	char new_path[LINE_MAX];
	strcpy(new_path, packagepath);
	strcat(new_path, path);
	/* existance: whether this item has existed in the target package. */
	int existance = 0;
	/* if the item has existed in the target package and the copy degree is `metadatacopy`, it is done! */
	if(access(new_path, F_OK) == 0) {
		existance = 1;
		if(fullcopy == 0) {
			fprintf(stdout, "`%s`: metadata copy, already exist!\n", path);
			return 0;
		}
	}

	struct stat source_stat;
	if(lstat(path, &source_stat) == -1) {
		fprintf(stdout, "lstat(`%s`): %s\n", path, strerror(errno));
		return -1;
	}

	if(S_ISREG(source_stat.st_mode)) {
		printf("`%s`: regular file\n", path);
		if(existance) {
			/* here, the copy degree must be fullcopy. */
			struct stat target_stat;
			if(stat(new_path, &target_stat) == -1) {
				fprintf(stdout, "stat(%s) fails: %s\n", new_path, strerror(errno));
				return -1;
			}
			/* Here is the tricky point: we use `truncate` system call to change the size of one empty file (`st_size`), but its `st_blocks` is still 0. */
			if(target_stat.st_blocks) {
				printf("`%s`: fullcopy exist! pass!\n", path);
			} else {
				remove(new_path);
				if(copy_file_to_file(path, new_path) < 0)
					fprintf(stdout, "copy_file_to_file from %s to %s fails: %s\n", path, new_path, strerror(errno));
				else
					printf("`%s`: fullcopy not exist, metadatacopy exist! create fullcopy ...\n", path);
			}
		} else {
			if(is_direntry == 0) {
				char tmppath[LINE_MAX], dir_name[LINE_MAX];
				strcpy(tmppath, path);
				strcpy(dir_name, dirname(tmppath));
				line_process(dir_name, "metadatacopy", 1, 0);
			}
			if(fullcopy) {
				if(copy_file_to_file(path, new_path) < 0)
					fprintf(stdout, "copy_file_to_file from %s to %s fails: %s\n", path, new_path, strerror(errno));
				else
					printf("`%s`: fullcopy not exist, metadatacopy not exist! create fullcopy ...\n", path);
			} else {
				FILE *fp = fopen(new_path, "w");
				if(fp != NULL)
					fclose(fp);
				else
					fprintf(stdout, "fopen(`%s`) fails: %s\n", new_path, strerror(errno));
				truncate(new_path, source_stat.st_size);
				printf("`%s`: metadatacopy not exist! create metadatacopy ...\n", path);
			}
		}
		/* copy the metadata info of the file */
		struct utimbuf time_buf;
		time_buf.modtime = source_stat.st_mtime;
		time_buf.actime = source_stat.st_atime;
		utime(new_path, &time_buf);
		chmod(new_path, source_stat.st_mode);
	} else if(S_ISDIR(source_stat.st_mode)) {
		fprintf(stdout, "`%s`: regular dir\n", path);
		if(is_direntry == 0) {
			mkpath(new_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH, 1);
			if(ignore_direntry == 0)
				create_dir_subitems(path, new_path);
		} else {
			mkdir(new_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		}
	} else if(S_ISLNK(source_stat.st_mode)) {
		/* first use `readlink` to obtain the target of the symlink. */
		char buf[LINE_MAX];
		int len;
		if ((len = readlink(path, buf, sizeof(buf)-1)) != -1) {
			buf[len] = '\0';
		} else {
			fprintf(stdout, "readlink(`%s`) fails: %s\n", path, strerror(errno));
			return -1;
		}
		printf("`%s`: symbolink, the direct real path: `%s`\n", path, buf);

		/* then obtain the complete path of the target of the symlink. */
		char linked_path[LINE_MAX], pathcopy[LINE_MAX], dir_name[LINE_MAX], newbuf[LINE_MAX];
		strcpy(pathcopy, path);
		strcpy(dir_name, dirname(pathcopy));
		if(buf[0] == '/') {
			strcpy(linked_path, buf);
		} else {
			strcpy(linked_path, dir_name);
			if(linked_path[strlen(linked_path) - 1] != '/')
				strcat(linked_path, "/");
			strcat(linked_path, buf);
		}
		fprintf(stdout, "the relative version of direct real path `%s` is: `%s`\n", path, linked_path);

		/* process the target of the symlink recursively. */
		if(fullcopy) {
			line_process(linked_path, "fullcopy", 0, 0);
		} else {
			line_process(linked_path, "metadatacopy", 1, 0);
		}

		/* ensure the directory of the symlink has been created in the target package. */
		if(is_direntry == 0) {
			char new_dir[LINE_MAX];
			strcpy(new_dir, packagepath);
			strcat(new_dir, dir_name);
			if(access(new_dir, F_OK) == -1) {
				fprintf(stdout, "the dir `%s` of the target of symbolink file `%s` does not exist, need to be created firstly", dir_name, path);
				line_process(dir_name, "metadatacopy", 1, 0);
			}
		}

		/* Transform absolute symlink into relative symlink. */
		if(buf[0] == '/') {
			relative_path(newbuf, buf, path);
			strcpy(buf, newbuf);
		}
		/* Create the symlink relationship */
		if(symlink(buf, new_path) == -1) {
			fprintf(stdout, "symlink from `%s` to `%s` create fail, %s\n", new_path, buf, strerror(errno));
		} else {
			fprintf(stdout, "create symlink from `%s` to `%s`.\n", new_path, buf);
		}
	}
	return 0;
}

/* copy the environment variable file into the package; create common-mountlist file. */
int post_process(char namelist_array[line_num][LINE_MAX]) {
	char new_envpath[LINE_MAX], common_mountlist[LINE_MAX];

	sprintf(new_envpath, "%s/%s", packagepath, envpath);
	copy_file_to_file(envpath, new_envpath);
	
	sprintf(common_mountlist, "%s/%s", packagepath, "common-mountlist");
	FILE *file;
	file = fopen(common_mountlist, "w");
	if(!file) {
		fprintf(stdout, "common-mountlist file `%s` can not be opened.", common_mountlist);
		exit(1);
	}
	fputs("/dev /dev\n", file);
	fputs("/misc /misc\n", file);
	fputs("/net /net\n", file);
	fputs("/proc /proc\n", file);
	fputs("/sys /sys\n", file);
	fputs("/var /var\n", file);
	fputs("/selinux /selinux\n", file);
	if(file)
		fclose(file);
	return 0;
}

int main(int argc, char *argv[])
{
	print_time();
	int c;

	struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"name-list", required_argument, 0, LONG_OPT_NAMELIST},
		{"env-path", required_argument, 0, LONG_OPT_ENVPATH},
		{"package-path", required_argument, 0, LONG_OPT_PACKAGEPATH},
		{0,0,0,0}
	};

	while((c=getopt_long(argc, argv, "h", long_options, NULL)) > -1) {
		switch(c) {
		case LONG_OPT_ENVPATH:
			envpath = optarg;
			break;
		case LONG_OPT_NAMELIST:
			namelist = optarg;
			break;
		case LONG_OPT_PACKAGEPATH:
			packagepath = optarg;
			break;
		default:
			show_help(argv[0]);
			exit(0);
			break;
		}
	}

	//preprocess: check whether the environment variable file exists; check whether the namelist file exists; check whether the package path exists;
	if((prepare_work()) != 0) {
		show_help(argv[0]);
		return -1;
	}
	char sorted_filename[LINE_MAX];
	sort_uniq_namelist(namelist, sorted_filename);
	line_num = line_number(sorted_filename);
	if(line_num <= 0) {
		fprintf(stdout, "The sorted namelist file is empty or can not be opened.\n");
		return -1;
	}
	char namelist_array[line_num][LINE_MAX];
	initialize_namelist_array(namelist_array, sorted_filename);
	//sort_namelist(namelist_array);
	int i, path_len;
	char path[LINE_MAX], *caller;
	for (i = 0; i < line_num; i++) {
		caller = strchr(namelist_array[i], '|') + 1;
		caller[strlen(caller) - 1] = '\0';
		path_len = strlen(namelist_array[i]) - strlen(caller) - 1;
		printf("%d --- namelist_array: %s; path_len: %d\n", i, namelist_array[i], path_len);
		strcpy(path, namelist_array[i]);
		path[path_len] = '\0';
		remove_final_slashes(path);
		line_process(path, caller, 0, 0);
	}
	post_process(namelist_array);
	print_time();
	return 0;
}
