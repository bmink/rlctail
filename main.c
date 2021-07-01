#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "bstr.h"
#include "blog.h"
#include "bcurl.h"


#define DEFAULT_CREDSFILE	".rlctailcreds.json"

#define MODE_NONE	0
#define MODE_LIST	1
#define MODE_TAIL	2

const char *usern = NULL;
const char *passw = NULL;

void usage(const char *);
int loadcreds(const char *);


int
main(int argc, char **argv)
{
	char	*execn;
	int	err;
	int	ret;
	int	mode;
	int	c;
	char	*subredditn;
	char	*postid;
	int	delaysec;
	char	*credsfile;
	
	err = 0;
	mode = MODE_NONE;
	delaysec = 0;
	credsfile = DEFAULT_CREDSFILE;

	execn = basename(argv[0]);
	if(xstrempty(execn)) {
		fprintf(stderr, "Can't get executable name\n");
		err = -1;
		goto end_label;
	}


        ret = blog_init(execn);
        if(ret != 0) {
                fprintf(stderr, "Could not initialize logging: %s\n",
                    strerror(ret));
		err = -1;
                goto end_label;
        }


	ret = bcurl_init();
        if(ret != 0) {
                fprintf(stderr, "Could not initialize curl: %s\n",
                    strerror(ret));
		err = -1;
                goto end_label;
        }
	

	if(argc < 2) {
		usage(execn);
		err = -1;
		goto end_label;
	}


	while ((c = getopt (argc, argv, "hc:d:l:t:")) != -1) {
		switch (c) {
		case 'h':
			usage(execn);
			goto end_label;

		case 'c':
			credsfile = optarg;
			break;

		case 'l':
			if(mode != MODE_NONE) {
				fprintf(stderr,
				    "More than one mode specified.\n");
				err = -1;
				goto end_label;
			}
			mode = MODE_LIST;
			subredditn = optarg;
			break;

		case 't':
			if(mode != MODE_NONE) {
				fprintf(stderr,
				    "More than one mode specified.\n");
				err = -1;
				goto end_label;
			}
			mode = MODE_TAIL;
			postid = optarg;
			break;

		case 'd':
			if(xstrempty(optarg)) {
				fprintf(stderr,
				    "Invalid delay specified.\n");
				err = -1;
				goto end_label;
			}
			delaysec = atoi(optarg);

			if(delaysec == 0) {
				fprintf(stderr,
				    "Delay has to be nonzero"
				    " (omit option for no delay).\n");
				err = -1;
				goto end_label;
			}

			break;


		case '?':
			fprintf (stderr, "Unknown option `-%c'\n", optopt);
			err = -1;
			goto end_label;
		default:
			fprintf (stderr, "Error while parsing options");
			err = -1;
			goto end_label;
		}
	}

	if(argc > optind) {
		fprintf (stderr, "Superfluous tailing arguments.\n");
		err = -1;
		goto end_label;
	}

	if(mode != MODE_TAIL && delaysec > 0) {
		fprintf (stderr, "Delay can only be specified"
		    " in tail mode.\n");
		err = -1;
		goto end_label;
	}

	if(xstrempty(credsfile)) {
		fprintf (stderr, "Invalid credentials file specified.\n");
		err = -1;
		goto end_label;
	}

	ret = loadcreds(credsfile);
	if(ret != 0) {
		fprintf (stderr, "Could not load credentials.\n");
		err = -1;
		goto end_label;
	}



end_label:

	bcurl_uninit();

	blog_uninit();

	return err;

	
}




void
usage(const char *execn)
{
	printf("usage:\n");
	printf("\n");
 	printf("  List current match threads:\n");
	printf("      %s [-c <credsfile>] -l <subreddit (sans '/r/'"
	    " prefix)>\n", execn);
	printf("\n");
 	printf("  Tail live comments on post:\n");
	printf("      %s [-c <credsfile>] [-d delaysec]"
	    " -t <postid>\n", execn);
	printf("\n");
 	printf("  Replay comments on post:\n");
	printf("      %s [-c <credsfile>] -t <time> -r <postid>\n", execn);
	printf("\n");
}


int
loadcreds(const char *credsfile)
{
	int		ret;
	struct stat	st;
	int		err;

	err = 0;
	memset(&st, 0, sizeof(struct stat));

	if(xstrempty(credsfile)) {
		err = EINVAL;
		goto end_label;
	}

	ret = stat(credsfile, &st);
	if(ret != 0) {
		blogf("Could not stat file %s: %s", credsfile, strerror(ret));
		err = ret;
		goto end_label;
	}

	if(!(st.st_mode & S_IFREG)) {
		blogf("%s is not a regular file", credsfile);
		err = EINVAL;
		goto end_label;
	}

	if((st.st_mode & (S_IRUSR | S_IWUSR)) != (S_IRUSR | S_IWUSR)) {
		blogf("Permissions for %s not correct, must be 600",
		    credsfile);
		err = EINVAL;
		goto end_label;
	}

	if(st.st_mode & (S_IXUSR | S_IRWXG | S_IRWXO)) {
		blogf("Permissions for %s not correct, must be 600",
		    credsfile);
		err = EINVAL;
		goto end_label;
	}


end_label:

	return err;

}
