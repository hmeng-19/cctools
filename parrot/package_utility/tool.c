#include <stdio.h>
#include "getopt.h"

enum {
	LONG_OPT_NAMELIST = 1,
	LONG_OPT_ENVPATH,
	LONG_OPT_PACKAGEPATH,
};

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

	const char *namelist;
	while((c=getopt_long(argc, argv, "", long_options, NULL)) > -1) {
		switch(c) {
		case LONG_OPT_ENVPATH:
			fprintf(stdout, "env-path: %s\n", optarg);
			break;	
		case LONG_OPT_NAMELIST:
			namelist = optarg;
			fprintf(stdout, "name-list: %s\n", optarg);
			fprintf(stdout, "name-list: %s\n", namelist);
			break;	
		case LONG_OPT_PACKAGEPATH:
			fprintf(stdout, "package-path: %s\n", optarg);
			break;	
		}
	}
	return 0;
}
