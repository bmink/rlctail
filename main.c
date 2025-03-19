#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "bstr.h"
#include "blog.h"
#include "bcurl.h"
#include "cJSON.h"
#include "cJSON_helper.h"
#include "reddit.h"


#define DEFAULT_USERCREDSFILE	".rlctail_usercreds.json"
#define DEFAULT_APPCREDSFILE	".rlctail_appcreds.json"
#define HTTP_USERAGENT		"macos:rlctail (by /u/brewbake)"

#define MODE_NONE	0
#define MODE_LIST	1
#define MODE_TAIL	2

void usage(const char *);


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
	char	*usercredsfile;
	char	*appcredsfile;
	
	err = 0;
	mode = MODE_NONE;
	delaysec = 0;
	usercredsfile = DEFAULT_USERCREDSFILE;
	appcredsfile = DEFAULT_APPCREDSFILE;
	subredditn = NULL;
	postid = NULL;

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


	if(argc < 2) {
		usage(execn);
		err = -1;
		goto end_label;
	}


	while ((c = getopt (argc, argv, "ha:u:d:l:t:f:")) != -1) {
		switch (c) {
		case 'h':
			usage(execn);
			goto end_label;

		case 'a':
			appcredsfile = optarg;
			break;

		case 'u':
			usercredsfile = optarg;
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

		case 'f':
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

	if(xstrempty(appcredsfile)) {
		fprintf (stderr, "Invalid app credentials file specified.\n");
		err = -1;
		goto end_label;
	}

	if(xstrempty(usercredsfile)) {
		fprintf (stderr, "Invalid user credentials file specified.\n");
		err = -1;
		goto end_label;
	}

	ret = reddit_init(usercredsfile, appcredsfile, HTTP_USERAGENT);
	if(ret != 0) {
		fprintf(stderr, "Could not set up Reddit\n");
		err = -1;
		goto end_label;
	}


end_label:

	blog_uninit();

	return err;
	
}




void
usage(const char *execn)
{
	printf("usage:\n");
	printf("\n");
 	printf("  List current match threads:\n");
	printf("      %s -l <subreddit (without '/r/'"
	    " prefix)>\n", execn);
	printf("\n");
 	printf("  Tail live comments on post:\n");
	printf("      %s [-d delaysec] -f <postid>\n", execn);
	printf("\n");
 	printf("  Replay comments on post:\n");
	printf("      %s -t <time> -r <postid>\n", execn);
	printf("\n");
	printf("  For all invocations, alternate credential files can be specified via:\n");
	printf("      [-u <usrcredsfile>]\n");
	printf("      [-a <appcredsfile>]\n");
	printf("\n");
}

