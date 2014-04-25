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

#define SIZE 256

int LINE_MAX=1024;

const char *namelist;
const char *packagepath;
const char *envpath;
char *sorted_namelist;
int line_num;

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
	namelist_file = fopen(filename, "r");
	if(!namelist_file)
		fprintf(stdout, "Can not open namelist file: %s", filename);
	int count;
	count = 0;
	char line[LINE_MAX];
	while(fgets(line, LINE_MAX, namelist_file) != NULL) {
		count++;
	}
	fprintf(stdout, "line number: %d\n", count);
	if(namelist_file)
		fclose(namelist_file);
	return count;
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
	fprintf(stdout, "relative_path: origin path: %s, oldpath: %s, newpath: %s\n", path, oldpath, newpath);
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

//sort all the lines of the namelist file.
void sort_namelist(char *namelist_array[line_num]) {
	int i;
	FILE *namelist_file = fopen(namelist, "r");
	char line[LINE_MAX];
	for (i = 0; fgets(line, LINE_MAX, namelist_file); i++) {
		namelist_array[i] = strdup(line);
	}
	qsort(namelist_array, line_num, sizeof(const char *), compare);
	if(namelist_file)
		fclose(namelist_file);
}

/* Function with behaviour like `mkdir -p'  */
//replace this func with the func under dttools
int mkpath(const char *path, mode_t mode) {
	char *pathcopy, *parent_dir;
	int rv;
	rv = -1;
	if(strcmp(path, ".") == 0 || strcmp(path, "/") == 0)
		return 0;

	if((pathcopy = strdup(path)) == NULL)
		exit(1);

	if((parent_dir = dirname(pathcopy)) == NULL)
		goto out;

	if((mkpath(parent_dir, mode) == -1) && (errno != EEXIST))
		goto out;

	if((mkdir(path, mode) == -1) && (errno != EEXIST))
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
	int status;
	status = mkpath(packagepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	return 0;
}

int copy_file(const char* source, const char* target)
{
	int input, output;
	if((input = open(source, O_RDONLY)) == -1)
	{
		return -1;
	}
	if((output = open(target, O_RDWR | O_CREAT)) == -1)
	{
		close(input);
		return -1;
	}

	off_t source_size = 0;
	struct stat input_stat;
	fstat(input, &input_stat);
	int result = sendfile(output, input, &source_size, input_stat.st_size);
	close(input);
	close(output);

	return result;
}

int is_special_caller(char *caller)
{
	int i;
	for(i = 0; i < special_caller_len; i++){
		if(strcmp(special_caller[i], caller) == 0) {
			return 1;
		}
	}
	return 0;
}

int is_special_path(const char *path)
{
	fprintf(stdout, "is_special_path: %s\n", path);
	int i;
	char *pathcopy, *first_dir, *tmp_dir;
	pathcopy = strdup(path);
	first_dir = strchr(pathcopy, '/') + 1;
	tmp_dir = strchr(first_dir, '/');
	int size;
	if(tmp_dir == NULL)
		size = strlen(first_dir);
	else
		size = strlen(first_dir) - strlen(tmp_dir);
	first_dir[size] = '\0';
	printf("first_dir: %s\n", first_dir);
	for(i = 0; i < special_path_len; i++){
		if(strcmp(special_path[i], first_dir) == 0) {
			return 1;
		}
	}
	return 0;
}
void print_permissions(char * dir_name)
{
	struct stat fileStat;
	lstat(dir_name, &fileStat);
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
}

int dir_entry(const char* filename)
{
	struct stat source_stat;
	char new_path[LINE_MAX];
	strcpy(new_path, packagepath);
	strcat(new_path, filename);
	if(access(new_path, F_OK) == 0) {
		fprintf(stdout, "%s already exists", new_path);
		return 0;
	}
	if(lstat(filename, &source_stat) == 0) {
		if(S_ISDIR(source_stat.st_mode)) {
			printf("%s, ---dir\n", filename);
			line_process(filename, "metadatacopy", 1);
		} else if (S_ISCHR(source_stat.st_mode)) {
			printf("%s, ---character\n", filename);
		} else if(S_ISBLK(source_stat.st_mode)) {
			printf("%s, ---block\n", filename);
		} else if(S_ISREG(source_stat.st_mode)) {
			printf("%s, ---regular file\n", filename);
			line_process(filename, "metadatacopy", 1);
/*
			FILE *fp = fopen(new_path, "w");
			fclose(fp);
			truncate(new_path, source_stat.st_size);
			struct utimbuf time_buf;
			time_buf.modtime = source_stat.st_mtime;
			time_buf.actime = source_stat.st_atime;
			utime(new_path, &time_buf);
			chmod(new_path, source_stat.st_mode);
*/
		} else if(S_ISFIFO(source_stat.st_mode)) {
			printf("%s, ---fifo special file\n", filename);
		} else if(S_ISLNK(source_stat.st_mode)) {
			//here recursively call dir_entry function
			printf("%s, ---link file\n", filename);
			line_process(filename, "metadatacopy", 1);
		} else if(S_ISSOCK(source_stat.st_mode)) {
			printf("%s, ---socket file\n", filename);
		}
	} else {
		fprintf(stderr, "stat(`%s'): %s\n", filename, strerror(errno));
	}
	return 0;
}


int create_dir_subitems(const char *path, char *new_path) {
	DIR *dir;
	struct dirent *entry;
	printf("enter into create_dir_subitems, path: %s\n", path);
	char *full_entrypath, *dir_name;
	dir_name = strdup(path);
	if(dir_name == NULL) {
		printf("error:%s\n", strerror(errno));
		return 0;
	}
	dir_name = realloc(dir_name, strlen(path) + 2);
	strcat(dir_name, "/");
	dir = opendir(path);
	printf("create_dir_subitems: %s\n", path);
	if (dir != NULL)
	{
		while (entry = readdir(dir))
		{
			full_entrypath = NULL;
			full_entrypath = strdup(dir_name);
			full_entrypath = realloc(full_entrypath, strlen(full_entrypath) + strlen(entry->d_name) + 1);
			strcat(full_entrypath, entry->d_name);
			dir_entry(full_entrypath);
		}
		closedir(dir);
	}
	else
		printf("Couldn't open the directory.\n");
	return 0;
}

int line_process(const char *path, char *caller, int ignore_direntry)
{
	printf("%s\n", path);
	if(is_special_path(path)) {
		fprintf(stdout, "Special path, ignore!\n");
		return 1;
	}
	int fullcopy = 0;
	ignore_direntry = 1;
	if(strcmp(caller,"metadatacopy") == 0) {
		fullcopy = 0;
	} else if(strcmp(caller,"fullcopy") == 0 || is_special_caller(caller)) {
		ignore_direntry = 0;
		fullcopy = 1;
	}
	char new_path[LINE_MAX];
	strcpy(new_path, packagepath);
	strcat(new_path, path);
	printf("new_path:%s \n", new_path);
	int existance = 0;
	if(access(new_path, F_OK) == 0) {
		existance = 1;
	}
	struct stat source_stat;
	if(lstat(path, &source_stat) == -1) {
		fprintf(stdout, "lstat execution fail. %s\n", strerror(errno));
		return 0;
	}

	if(S_ISREG(source_stat.st_mode)) {
		printf("regular file\n");
		if(existance) {
			if(fullcopy == 0) {
				printf("metadatacopy exist! pass!\n");
				goto regfiledone;
			} else {
				struct stat target_stat;
				stat(new_path, &target_stat);
				if(target_stat.st_blocks) {
					printf("fullcopy exist! pass!\n");
					goto regfiledone;
				} else {
					printf("fullcopy not exist, metadatacopy exist! create fullcopy ...\n");
					copy_file(path, new_path);
				}
			}
		} else {
			char tmppath[LINE_MAX], dir_name[LINE_MAX];
			strcpy(tmppath, path);
			strcpy(dir_name, dirname(tmppath));
			line_process(dir_name, "metadatacopy", 1);
			if(fullcopy) {
				printf("fullcopy not exist, metadatacopy not exist! create fullcopy ...\n");
				copy_file(path, new_path);
			}
			else {
				printf("metadatacopy not exist! create metadatacopy ...\n");
				FILE *fp = fopen(new_path, "w");
				fclose(fp);
				truncate(new_path, source_stat.st_size);
			}
		}
		struct utimbuf time_buf;
		time_buf.modtime = source_stat.st_mtime;
		time_buf.actime = source_stat.st_atime;
		utime(new_path, &time_buf);
		chmod(new_path, source_stat.st_mode);
regfiledone:
		return 0;
	}
	if(S_ISDIR(source_stat.st_mode)) {
		printf("regular dir\n");
		mkpath(new_path, source_stat.st_mode);
		if(ignore_direntry == 0)
			create_dir_subitems(path, new_path);
		return 0;
	}
	if(S_ISLNK(source_stat.st_mode)) {
		char actualpath[LINE_MAX];
		realpath(path, actualpath);
		char buf[LINE_MAX];
		int len;
		if ((len = readlink(path, buf, sizeof(buf)-1)) != -1)
			buf[len] = '\0';
		printf("symbolink, the real path: %s\n", actualpath);
		printf("symbolink, the direct real path: %s\n", buf);
		char linked_path[LINE_MAX];
		char dir_name[LINE_MAX];
		char pathcopy[LINE_MAX];
		strcpy(pathcopy, path);
		strcpy(dir_name, dirname(pathcopy));
		//realpath(buf, linked_path);
		if(buf[0] == '/')
			strcpy(linked_path, buf);
		else {
			//remove the duplicated / and .
			//chdir(dir_name);
			strcpy(linked_path, dir_name);
			if(linked_path[strlen(linked_path) - 1] != '/')
				strcat(linked_path, "/");
			strcat(linked_path, buf);
			}
		fprintf(stdout, "the realpath of direct real path is: %s\n", linked_path);
		if(fullcopy)
			line_process(linked_path, "fullcopy", 0);
		else
			line_process(linked_path, "metadatacopy", 1);
		char new_dir[LINE_MAX];
		strcpy(new_dir, packagepath);
		strcat(new_dir, dir_name);
		if(access(new_dir, F_OK) == -1) {
			fprintf(stdout, "new_dir %s  does not exist, need to be created firstly", dir_name);
			line_process(dir_name, "metadatacopy", 1);
		}
/*
		if(chdir(new_dir) == -1) {
			fprintf(stdout, "chdir fails\n");
			return 0;
		}
		fprintf(stdout, "current dir: %s\n", getcwd(0, 0));
*/
		char newbuf[LINE_MAX];
		if(buf[0] == '/') {
			relative_path(newbuf, buf, path);
			strcpy(buf, newbuf);
		}
		if(symlink(buf, new_path) == -1)
			fprintf(stdout, "symlink create fail, %s\n", strerror(errno));
		return 0;
	}
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
			fprintf(stdout, "env-path: %s\n", optarg);
			break;
		case LONG_OPT_NAMELIST:
			namelist = optarg;
			fprintf(stdout, "name-list: %s\n", optarg);
			fprintf(stdout, "name-list: %s\n", namelist);
			break;
		case LONG_OPT_PACKAGEPATH:
			packagepath = optarg;
			fprintf(stdout, "package-path: %s\n", optarg);
			break;
		default:
			show_help(argv[0]);
			exit(0);
			break;
		}
	}

	//
	fprintf(stdout, "special_path num: %d; special_caller num: %d \n", special_path_len, special_caller_len);
	//preprocess: check whether the environment variable file exists; check whether the list namelist file exists; check whether the package path exists;
	//sorting the listfile(single func);
	int result;
	result = prepare_work();
	if(result != 0) {
		show_help(argv[0]);
		return -1;
	}

	line_num = line_number(namelist);
	char *namelist_array[line_num];
	sort_namelist(namelist_array);
	int i;
	int path_len;
	char path[LINE_MAX], *caller;
	for (i = 0; i < line_num; i++) {
		caller = strchr(namelist_array[i], '|') + 1;
		caller[strlen(caller) - 1] = '\0';
		path_len = strlen(namelist_array[i]) - strlen(caller) - 1;
		strcpy(path, namelist_array[i]);
		printf ("%d --- namelist_array: %s; path_len: %d\n", i, namelist_array[i], path_len);
		path[path_len] = '\0';
		remove_final_slashes(path);
		printf("path: %s;  (%d)  caller: %s  (%d)\n", path, strlen(path), caller, strlen(caller));
		line_process(path, caller, 0);
	}
	//obtain relative path of one absolute path;
/*	char line1[LINE_MAX];
	strcpy(line1, "/dir/file/abc/soft");
	char *line2;
	//relative_path(line2, line1);
	printf("%s %s\n", line1, line2);
*/
	//delete the final / of one path;
/*
	char testline[LINE_MAX];
	strcpy(testline,  "abc/def////");
	char *newline;
	fprintf(stdout, "final path is: %s\n", newline);
*/
	print_time();
	return 0;
}













