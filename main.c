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

void usage(const char *);

#define MAX_COMMENTS	20 

bstr_t	*last_comment_id = NULL;

#define MAX_AUTHORNAME_LEN	10
#define MAX_BODY_LEN		70

int
print_new_comments(const char *subredditn, const char *postid)
{

	barr_t			*comments;
	reddit_comment_t	*comment;
	reddit_comment_t	*start;
	int			ret;
	int			err;
	bstr_t			*val;

	if(xstrempty(subredditn) || xstrempty(postid))
		return EINVAL;

	err = 0;
	comments = NULL;
	val = NULL;

	comments = barr_init(sizeof(reddit_comment_t));
	if(comments == NULL) {
		blogf("Could not allocate comments");
		err = ENOMEM;
		goto end_label;
	}

	ret = reddit_get_new_comments(subredditn, postid, comments,
	    MAX_COMMENTS);

	if(ret != 0) {
		blogf("Could not get new comments");
		err = ret;
		goto end_label;
	}

	if(barr_cnt(comments) == 0)
		goto end_label;

		
	for(start = (reddit_comment_t *)barr_begin(comments);
	    start < (reddit_comment_t *)barr_end(comments);
	    ++start) {
		if(!bstrcmp(start->rc_id, bget(last_comment_id)))
			break;
	}

	if(start == (reddit_comment_t *)barr_begin(comments)) {
		/* No new comments since last seen one */
		goto end_label;
	}

	val = binit();
	if(val == NULL) {
		blogf("Could not allocate val");
		err = ENOMEM;
		goto end_label;
	}
	
	for(comment = start - 1;
	    comment >= (reddit_comment_t *) barr_begin(comments); --comment) {

		bstrtomaxlen(comment->rc_author, val, MAX_AUTHORNAME_LEN, 0);
		bstrpad(val, MAX_AUTHORNAME_LEN, ' ');
		printf("(%s)-> ", bget(val));
		bstrtomaxlen(comment->rc_body, val, MAX_BODY_LEN, 0);
		printf("%s\n", bget(val));
	}

	if(barr_cnt(comments) > 0) {
		bclear(last_comment_id);	
		bstrcat(last_comment_id, bget(
		    ((reddit_comment_t *)barr_elem(comments, 0))->rc_id));
	}


end_label:

	if(comments) {
		for(comment = (reddit_comment_t *)barr_begin(comments);
		    comment < (reddit_comment_t *)barr_end(comments);
		    ++comment) {
			buninit(&(comment->rc_author));
			buninit(&(comment->rc_body));
		}
		barr_uninit(&comments);
	}

	buninit(&val);

	return err;
}

int
main(int argc, char **argv)
{
	char	*execn;
	int	err;
	int	ret;
	int	c;
	char	*redditurl;
	bstr_t	*subredditn;
	bstr_t	*postid;
	int	delaysec;
	char	*usercredsfile;
	char	*appcredsfile;
	barr_t	*urlparts;
	bstr_t 	*elem;
	
	err = 0;
	delaysec = 0;
	usercredsfile = DEFAULT_USERCREDSFILE;
	appcredsfile = DEFAULT_APPCREDSFILE;
	subredditn = NULL;
	postid = NULL;
	redditurl = NULL;
	urlparts = NULL;

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

	while ((c = getopt (argc, argv, "ha:u:d:")) != -1) {
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

	if(argc <= optind) {
		fprintf (stderr, "No Reddit post specified.\n");
		err = -1;
		goto end_label;
	}

	if(argc != optind + 1) {
		fprintf (stderr, "Can only specify one Reddit post.\n");
		err = -1;
		goto end_label;
	}

	redditurl = argv[optind];

	if(xstrempty(redditurl)) {
		fprintf (stderr, "Invalid (empty?) Reddit URL.\n");
		err = -1;
		goto end_label;
	}

	subredditn = binit();
	if(subredditn == NULL) {
		fprintf(stderr, "Could not allocate subredditn");
		err = -1;
		goto end_label;
	}

	postid = binit();
	if(postid == NULL) {
		fprintf(stderr, "Could not allocate postid");
		err = -1;
		goto end_label;
	}

	ret = xstrsplit(redditurl, "/", 0, &urlparts);
	if(ret != 0) {
		fprintf (stderr, "Could not split Reddit URL\n");
		err = -1;
		goto end_label;
	}

	if(urlparts == NULL || barr_cnt(urlparts) < 7) {
		fprintf (stderr, "Reddit URL is malformed\n");
		err = -1;
		goto end_label;
	}

	bstrcat(subredditn, bget((bstr_t *)barr_elem(urlparts, 4)));
	bstrcat(postid, bget((bstr_t *)barr_elem(urlparts, 6)));

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

	last_comment_id = binit();
	if(last_comment_id == NULL) {
		fprintf(stderr, "Could not allocate last_comment_id");
		err = -1;
		goto end_label;
	}


	while(1) {
		ret = print_new_comments(bget(subredditn), bget(postid));
		if(ret != 0) {
			fprintf(stderr, "Error while printing new comments\n");
			err = -1;
			goto end_label;
		}

		sleep(1);
	}


end_label:

	reddit_uninit();
	blog_uninit();
	buninit(&subredditn);
	buninit(&postid);

	if(urlparts) {
		for(elem = (bstr_t *)barr_begin(urlparts);
		    elem < (bstr_t *)barr_end(urlparts); ++elem) {
			buninit_(elem);
		}
		barr_uninit(&urlparts);
	}

	buninit(&last_comment_id);

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

