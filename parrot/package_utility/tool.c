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

/* Compare the strings. */
static int compare (const void * a, const void * b)
{
	/* The pointers point to offsets into "namelist_array", so we need to dereference them to get at the strings. */
	return strcmp (*(const char **) a, *(const char **) b);
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
		count ++;
	}
	fprintf(stdout, "line number: %d\n", count);
	if(namelist_file)
		fclose(namelist_file);
	return count;
}

void relative_path(char *newpath, const char *oldpath)
{
	char *s;
	strcpy(newpath, "");
	s = strchr(oldpath, '/');
	while(s != NULL) {
		s++;
		strcat(newpath, "../");
		s = strchr(s, '/');
	}
	newpath[strlen(newpath) - 1] = '\0';
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


//sort all the lines of the namelist file.
void sort_namelist(char *namelist_array[line_num]) {
	int i;
	FILE *namelist_file = fopen(namelist, "r");
	char line[LINE_MAX];
	i = 0;
	while (fgets(line, LINE_MAX, namelist_file) != NULL) {
		namelist_array[i++] = strdup(line);
	}
	qsort(namelist_array, line_num, sizeof(const char *), compare);
	
	if(namelist_file)
		fclose(namelist_file);
}

/* Function with behaviour like `mkdir -p'  */
int mkpath(const char *s, mode_t mode) {
        char *q, *r = NULL, *path = NULL, *up = NULL;
        int rv;

        rv = -1;
        if (strcmp(s, ".") == 0 || strcmp(s, "/") == 0)
                return (0);

        if ((path = strdup(s)) == NULL)
                exit(1);
     
        if ((q = strdup(s)) == NULL)
                exit(1);

        if ((r = dirname(q)) == NULL)
                goto out;
        
        if ((up = strdup(r)) == NULL)
                exit(1);

        if ((mkpath(up, mode) == -1) && (errno != EEXIST))
                goto out;
	
		if ((mkdir(path, mode) == -1) && (errno != EEXIST))
                rv = -1;
        else
                rv = 0;

out:
        if (up != NULL)
                free(up);
        free(q);
        free(path);
        return (rv);
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

int CopyFile(const char* source, const char* destination)
{    
    int input, output;    
    if ((input = open(source, O_RDONLY)) == -1)
    {
        return -1;
    }    
    if ((output = open(destination, O_RDWR | O_CREAT)) == -1)
    {
        close(input);
        return -1;
    }

    off_t bytesCopied = 0;
    struct stat fileinfo = {0};
    fstat(input, &fileinfo);
        struct stat outputinfo ={0};
    int result = sendfile(output, input, &bytesCopied, fileinfo.st_size);
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

int is_special_path(char *path)
{
	int i;
	char *pathcopy, *first_dir, *tmp_dir;
	pathcopy = strdup(path);
	first_dir = strchr(pathcopy, '/') + 1;
	tmp_dir = strchr(first_dir, '/');
	int size;
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
	stat(dir_name, &fileStat);
	printf("File Permissions: %s\t", dir_name);
    printf( (S_ISDIR(fileStat.st_mode)) ? "d" : "-");
    printf( (fileStat.st_mode & S_IRUSR) ? "r" : "-");
    printf( (fileStat.st_mode & S_IWUSR) ? "w" : "-");
    printf( (fileStat.st_mode & S_IXUSR) ? "x" : "-");
    printf( (fileStat.st_mode & S_IRGRP) ? "r" : "-");
    printf( (fileStat.st_mode & S_IWGRP) ? "w" : "-");
    printf( (fileStat.st_mode & S_IXGRP) ? "x" : "-");
    printf( (fileStat.st_mode & S_IROTH) ? "r" : "-");
    printf( (fileStat.st_mode & S_IWOTH) ? "w" : "-");
    printf( (fileStat.st_mode & S_IXOTH) ? "x" : "-");
    printf("\n\n");
}

int file_type(const char* filename)
{
	struct stat st;
	stat(filename, &st);
	if(S_ISDIR(st.st_mode))
	{
	    puts("---dir");
	    return 0;
	}

	if(S_ISCHR(st.st_mode))
	{
		puts("---character");
		return 0;
	}

	if(S_ISBLK(st.st_mode))
	{
		puts("---block");
		return 0;
	}

	if(S_ISREG(st.st_mode))
	{
		puts("---regular file");
		return 0;
	}

	if(S_ISFIFO(st.st_mode))
	{
		puts("---fifo special file");
		return 0;
	}

	if(S_ISLNK(st.st_mode))
	{
		puts("---link file");
		return 0;
	}

	if(S_ISSOCK(st.st_mode))
	{
		puts("---socket file");
		return 0;
	}

}


int create_dir_subitems(char *path, char *new_path) {
  DIR *dp;
  struct dirent *ep;

	printf("create_dir_subitems: %s\n", path);
  dp = opendir (path);
  if (dp != NULL)
    {
      while (ep = readdir (dp))
    {
    puts (ep->d_name);
    file_type(ep->d_name);
    }
      (void) closedir (dp);
    }
  else
    puts ("Couldn't open the directory.");

  return 0;
}

int line_process(char *path, char *caller)
{
	struct stat source_stat;
	lstat(path, &source_stat);
	printf("%s\n", path);
	char new_path[LINE_MAX];
	strcpy(new_path, packagepath);
	if(is_special_path(path)) {
		fprintf(stdout, "Special path, ignore!\n");
		return 0;
	}
	if(S_ISREG(source_stat.st_mode)) {
		printf("regular file\n");
		strcat(new_path, path);
		printf("new_path:%s \n", new_path);
		char pathcopy[LINE_MAX];
		strcpy(pathcopy, new_path);
		char *dir_name;
		dir_name = dirname(pathcopy);
		struct stat dir_stat;
		char *tmppath;
		tmppath = strdup(path);
		stat(dirname(tmppath), &dir_stat);
		printf("dir_name: %s\n", dir_name);
		mkpath(dir_name, dir_stat.st_mode);
		print_permissions(dir_name);
		print_permissions(tmppath);
		chmod(dir_name, dir_stat.st_mode);

		if(is_special_caller(caller)) {
			if(access(new_path, F_OK) == -1) {
				printf("special caller full copy, not exist\n");
				CopyFile(path, new_path);
			}
			else {
				struct stat target_stat;
				stat(new_path, &target_stat);
				if(target_stat.st_blocks)
					printf("special caller full copy, exist content\n");
				else {
					printf("special caller full copy, exist only metadata\n");
					CopyFile(path, new_path);
				}
			}
		}
		else {
			if(access(new_path, F_OK) != -1) {
				fprintf(stdout, "not full copy, and already exist\n");
				return 0;
			}
			printf("not full copy, does not exist\n");
			FILE *fp = fopen(new_path, "w");
			fclose(fp);
			truncate(new_path, source_stat.st_size);
		}
		struct utimbuf time_buf;
		time_buf.modtime = source_stat.st_mtime;
		time_buf.actime = source_stat.st_atime;
		utime(new_path, &time_buf);
		chmod(new_path, source_stat.st_mode);
		return 0;
	}
	if(S_ISDIR(source_stat.st_mode)) {
		printf("regular dir\n");
		strcat(new_path, path);
		printf("newpath:%s \n", new_path);
		struct stat path_stat;
		mkpath(new_path, path_stat.st_mode);
		create_dir_subitems(path, new_path);
		return 0;
	}
	if(S_ISLNK(source_stat.st_mode)) {
		char actualpath[LINE_MAX];
		realpath(path, actualpath);
		strcat(new_path, path);
		printf("symbolink, the real path: %s\n", actualpath);
		char buf[LINE_MAX];
		int len;
		if ((len = readlink(path, buf, sizeof(buf)-1)) != -1)
			buf[len] = '\0';
		printf("symbolink, the direct real path: %s\n", buf);
		printf("newpath:%s \n", new_path);
		return 0;
	}

}

int main(int argc, char *argv[])
{
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
	for (i = 0; i < line_num; i++) {
		printf ("%d --- %s", i, namelist_array[i]);
		char *caller;
		caller = strchr(namelist_array[i], '|') + 1;
		caller[strlen(caller) - 1] = '\0';
		path_len = strlen(namelist_array[i]) - strlen(caller);
		char *path;
		strncpy(path, namelist_array[i], path_len);
		path[path_len - 1] = '\0';
		remove_final_slashes(path);
		printf("path: %s;  (%d)  caller: %s  (%d)\n", path, strlen(path), caller, strlen(caller));
		line_process(path, caller);
	}



	//obtain relative path of one absolute path;
/*	char line1[LINE_MAX];
	strcpy(line1, "/dir/file/abc/soft");
	char *line2;
	relative_path(line2, line1);
	printf("%s %s\n", line1, line2);
*/
	//delete the final / of one path;
/*
	char testline[LINE_MAX];
	strcpy(testline,  "abc/def////");
	char *newline;
	fprintf(stdout, "final path is: %s\n", newline);
*/
	return 0;
}













