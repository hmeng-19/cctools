#include <stdio.h>
#include <string.h>
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

int line_process(char *path, char *caller)
{
	struct stat source_stat;
	lstat(path, &source_stat);
	printf("%s\n", path);
	char new_path[LINE_MAX];
	strcpy(new_path, packagepath);
	if(S_ISREG(source_stat.st_mode)) {
		printf("regular file\n");
		strcat(new_path, path);
		printf("new_path:%s \n", new_path);
		char pathcopy[LINE_MAX];
		strcpy(pathcopy, new_path);
		mkpath(dirname(pathcopy), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		printf("after dirname, new_path:%s \n", new_path);
		CopyFile(path, new_path);
		return 0;
	}
	if(S_ISDIR(source_stat.st_mode)) {
		printf("regular dir\n");
		strcat(new_path, path);
		printf("newpath:%s \n", new_path);
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













