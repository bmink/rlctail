#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "bstr.h"
#include "blog.h"
#include "bcurl.h"
#include "cJSON.h"
#include "cJSON_helper.h"
#include "reddit.h"
#include "blist.h"

#define DEFAULT_USERCREDSFILE	".rlctail_usercreds.json"
#define DEFAULT_APPCREDSFILE	".rlctail_appcreds.json"
#define HTTP_USERAGENT		"macos:rlctail (by /u/brewbake)"

void usage(const char *);

#define MAX_COMMENTS	20 

blist_t	*	pending_comments = NULL;
pthread_mutex_t	pending_comments_mutex;

#define MAX_AUTHORNAME_LEN	10

bstr_t	*subredditn = NULL;
bstr_t	*postid = NULL;

int		do_shutdown = 0;
pthread_mutex_t	do_shutdown_mutex;

#define PRINTER_SLEEP_US	30 * 1000	/* 50 ms */
#define MAX_PENDING		100


void
freecomment(reddit_comment_t *rc)
{
	if(rc == NULL)
		return;

	buninit(&rc->rc_id);
	buninit(&rc->rc_author);
	buninit(&rc->rc_body);
	free(rc);
}


void *
comment_getter(void *arg)
{
	int			doexit;
	int			ret;
	bstr_t			*last_comment_id;
	barr_t			*comments;
	reddit_comment_t	*comment;
	reddit_comment_t	*commcopy;
	reddit_comment_t	*start;
	int			added;

	doexit = 0;	
	last_comment_id = NULL;
	comments = NULL;

	last_comment_id = binit();
	if(last_comment_id == NULL) {
		blogf("Could not allocate last_comment_id");
		goto end_label;
	}

	while(1) {

		/* Check if we need to shut down */
		ret = pthread_mutex_lock(&do_shutdown_mutex);
		if(ret != 0) {
			blogf("Couldn't lock mutex, exiting thread");
			return NULL;
		}
		if(do_shutdown)
			++doexit;
		ret = pthread_mutex_unlock(&do_shutdown_mutex);
		if(ret != 0) {
			blogf("Couldn't unlock mutex, exiting thread");
			return NULL;
		}

		if(doexit)
			break;
	
		comments = barr_init(sizeof(reddit_comment_t));
		if(comments == NULL) {
			blogf("Could not allocate comments");
			goto end_label;
		}

		ret = reddit_get_new_comments(bget(subredditn), bget(postid),
		    comments, MAX_COMMENTS);

		if(ret != 0) {
			blogf("Could not get new comments");
			printf("Could not get new comments: %s\n",
			    strerror(ret));
			goto cont_label;
		}

		if(barr_cnt(comments) == 0)
			goto cont_label;


		for(start = (reddit_comment_t *)barr_begin(comments);
		    start < (reddit_comment_t *)barr_end(comments);
		    ++start) {
			if(!bstrcmp(start->rc_id,
			    bget(last_comment_id)))
			break;
		}

		if(start == (reddit_comment_t *)barr_begin(comments)) {
			/* No new comments since last seen one */
			goto cont_label;
		}

		/* There are new comments. First save the last seen comment's
		 * ID */
		bclear(last_comment_id);	
		bstrcat(last_comment_id, bget(
		    ((reddit_comment_t *) barr_elem(comments, 0))->rc_id));

		/* Now add new comments to pending queue */
		for(comment = start - 1;
		    comment >= (reddit_comment_t *) barr_begin(comments);
		    --comment) {

			/* This is a little tricky... we allocate a new
			 * comment and copy the contents of the cursor
			 * into it. Then zero out the cursor's contents.
			 * This way we can add the new comment to the
			 * blist and also can clean up the array of comments
			 * in a bit */
			
			commcopy = malloc(sizeof(reddit_comment_t));
			if(commcopy == NULL) {
				blogf("Couldn't lmutex, exiting thread");
				return NULL;
			}
			*commcopy = *comment;
		
			memset(comment, 0, sizeof(reddit_comment_t));

			/* Add new comment to pending queue (if not full)  */
			ret = pthread_mutex_lock(&pending_comments_mutex);
			if(ret != 0) {
				blogf("Couldn't lock mutex, exiting thread");
				return NULL;
			}

			added = 0;
			if(blist_cnt(pending_comments) < MAX_PENDING) {
				blist_rpush(pending_comments, (void *)commcopy);
				if(ret != 0) {
					blogf("Couldn't add comment to pending list");
				}
				++added;
			}

			ret = pthread_mutex_unlock(&pending_comments_mutex);
			if(ret != 0) {
				blogf("Couldn't lock mutex, exiting thread");
				return NULL;
			}

			if(!added) {
				/* Pending queue is full, we discard the
				 * comment. We could, or course, hold
				 * "infinite" (or at least a very large
				 * number of pending comments. But it's
				 * actually better to start shedding comments
				 * sooner rather than later. If comments are
				 * coming in too fast, the screen would either
				 * have to scroll too fast for reading,
				 * or we'd fall behind. By observing a (not
				 * too large) upper limit for the pending
				 * queue, we essentially throttle the number
				 * of comments the screen will show, keeping
				 * things up to date and readable. Some
				 * comments will get thrown out, yes, but
				 * that's OK. The user will get the gist of
				 * what's going on by reading the comments
				 * that do get displayed. */
				freecomment(commcopy);
			}

		}


cont_label:

		if(comments) {
			for(comment = (reddit_comment_t *)barr_begin(comments);
			    comment < (reddit_comment_t *)barr_end(comments);
			    ++comment) {
				buninit(&(comment->rc_id));
				buninit(&(comment->rc_author));
				buninit(&(comment->rc_body));
			}
			barr_uninit(&comments);
		}

		sleep(1);
	}


end_label:
	if(last_comment_id)
		buninit(&last_comment_id);

	if(comments) {
		for(comment = (reddit_comment_t *)barr_begin(comments);
		    comment < (reddit_comment_t *)barr_end(comments);
		    ++comment) {
			buninit(&(comment->rc_id));
			buninit(&(comment->rc_author));
			buninit(&(comment->rc_body));
		}
		barr_uninit(&comments);
	}

	return NULL;
}


void *
comment_printer(void *arg)
{
	int			doexit;
	int			ret;
	int			pendcnt;
	reddit_comment_t	*comment;
	bstr_t			*val;
	struct winsize		wins;

	doexit = 0;

	val = binit();
	if(val == NULL) {
		blogf("Couldn't allocate val, exiting thread");
		return NULL;
	}

	/* Hide cursor. */
	printf("\e[?25l");

	while(1) {

		/* Check if we need to shut down */
		ret = pthread_mutex_lock(&do_shutdown_mutex);
		if(ret != 0) {
			blogf("Couldn't lock mutex, exiting thread");
			return NULL;
		}
		if(do_shutdown)
			++doexit;
		ret = pthread_mutex_unlock(&do_shutdown_mutex);
		if(ret != 0) {
			blogf("Couldn't unlock mutex, exiting thread");
			return NULL;
		}

		if(doexit)
			break;

		/* Get terminal width */
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &wins);

		ret = pthread_mutex_lock(&pending_comments_mutex);
		if(ret != 0) {
			blogf("Couldn't lock mutex, exiting thread");
			return NULL;
		}

		comment = (reddit_comment_t *) blist_lpop(pending_comments);
		pendcnt = blist_cnt(pending_comments);

		ret = pthread_mutex_unlock(&pending_comments_mutex);
		if(ret != 0) {
			blogf("Couldn't lock mutex, exiting thread");
			return NULL;
		}

		if(comment == NULL)
			goto cont_label;

		bclear(val);
		bstrtomaxlen(comment->rc_author, val, MAX_AUTHORNAME_LEN, 0);
		bstrpad(val, MAX_AUTHORNAME_LEN, ' ');
		printf("\n(%s)-> ", bget(val));
		bstrrepl(comment->rc_body, "\n", "↲");
		bstrtomaxlen_utf8(comment->rc_body, val,
		    wins.ws_col - 5 - MAX_AUTHORNAME_LEN, 0);
		printf("%s", bget(val));
		/* No newline so we print all the way to the bottom of the
		 * screen. But then we fflush() to make sure all text gets
		 * printed. */
		fflush(stdout);

		freecomment(comment);

cont_label:

#if 1
		printf("\e7");			/* Save cursor position */
		printf("\e[0;%dH", wins.ws_col - 11); /* Go to top right */
		printf("\e[7m");		/* Invert colors */
		printf("Pending:%3d \n", pendcnt);
		printf("\e[27m");		/* Restore colors */
		printf("\e8");			/* Restore cursor position */
		fflush(stdout);
#endif
		usleep(PRINTER_SLEEP_US);

	}

	/* Reenable cursor */
	printf("\e[?12l\e[?25h");

	return NULL;

}






int
main(int argc, char **argv)
{
	char		*execn;
	int		err;
	int		ret;
	int		c;
	char		*redditurl;
	int		delaysec;
	char		*usercredsfile;
	char		*appcredsfile;
	barr_t		*urlparts;
	bstr_t 		*elem;
	reddit_comment_t *rc;
	int		sh_mutex_inited;
	int		pc_mutex_inited;
	pthread_t	getter;
	pthread_t	printer;
	sigset_t	sigmask;
	int		sig;

	
	err = 0;
	delaysec = 0;
	usercredsfile = DEFAULT_USERCREDSFILE;
	appcredsfile = DEFAULT_APPCREDSFILE;
	subredditn = NULL;
	postid = NULL;
	redditurl = NULL;
	urlparts = NULL;
	sh_mutex_inited = 0;
	pc_mutex_inited = 0;

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
	if(bstrempty(subredditn)) {
		fprintf (stderr, "Invalid subreddit name\n");
		err = -1;
		goto end_label;
	}

	bstrcat(postid, bget((bstr_t *)barr_elem(urlparts, 6)));
	if(bstrempty(postid)) {
		fprintf (stderr, "Invalid post ID\n");
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

	pending_comments = blist_init();
	if(pending_comments == NULL) {
		fprintf(stderr, "Could not allocate pending_comments");
		err = -1;
		goto end_label;
	}

	ret = pthread_mutex_init(&pending_comments_mutex, NULL);
	if(ret != 0) {
		fprintf(stderr, "Could not initialize comments mutex");
		err = -1;
		goto end_label;
	}
	++pc_mutex_inited;

	ret = pthread_mutex_init(&do_shutdown_mutex, NULL);
	if(ret != 0) {
		fprintf(stderr, "Could not initialize shutdown mutex");
		err = -1;
		goto end_label;
	}
	++sh_mutex_inited;

	/* Block signals we want to wait for */
	sigemptyset(&sigmask);
	sigaddset (&sigmask, SIGINT);
	sigaddset (&sigmask, SIGTERM);

	ret = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);	
	if(ret != 0) {
		fprintf(stderr, "Could not set signal mask\n");
		err = -1;
		goto end_label;
	}

	ret = pthread_create(&getter, NULL, comment_getter, NULL);
	if(ret != 0) {
		fprintf(stderr, "Could not create getter thread\n");
		err = -1;
		goto end_label;
	}

	ret = pthread_create(&printer, NULL, comment_printer, NULL);
	if(ret != 0) {
		fprintf(stderr, "Could not create printer thread\n");
		err = -1;
		goto end_label;
	}

	while(!do_shutdown) {

		ret = sigwait(&sigmask, &sig);
		if(ret != 0) {
			fprintf(stderr, "sigwait error\n");
			err = -1;
			goto end_label;
		}
	
		switch (sig) {
		case SIGINT:
		case SIGTERM:
			ret = pthread_mutex_lock(&do_shutdown_mutex);
			if(ret != 0) {
				fprintf(stderr, "Couldn't lock mutex\n");
				err = -1;
				goto end_label;
			}
			++do_shutdown;
			ret = pthread_mutex_unlock(&do_shutdown_mutex);
			if(ret != 0) {
				fprintf(stderr, "Couldn't unlock mutex\n");
				err = -1;
				goto end_label;
			}
			break;
		default:
			/* We should never be here. */
			blogf("Unexpected signal caught: %d", sig);
			continue;
		}
  	}


	ret = pthread_join(getter, NULL);
	if(ret != 0) {
		fprintf(stderr, "Could not join getter thread");
		err = -1;
		goto end_label;
	}

	ret = pthread_join(printer, NULL);
	if(ret != 0) {
		fprintf(stderr, "Could not join printer thread");
		err = -1;
		goto end_label;
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

	if(pending_comments) {
		while((rc = (reddit_comment_t *)blist_lpop(pending_comments)))
			freecomment(rc);
		blist_uninit(&pending_comments);
	}	

	if(pc_mutex_inited)
		pthread_mutex_destroy(&pending_comments_mutex);
	if(sh_mutex_inited)
		pthread_mutex_destroy(&do_shutdown_mutex);

	return err;
}




void
usage(const char *execn)
{
	printf("usage:\n");
	printf("\n");
 	printf("  Tail live comments on post:\n");
	printf("      %s <reddit_post_url>\n", execn);
	printf("\n");
	printf("  Supported options:\n");
	printf("\n");
	printf("      -d <delaysec>\n");
	printf("              Delay comment display by this many seconds\n");
	printf("\n");
	printf("      -f      Full comment display mode  (default is compact"
	    " display)\n");
	printf("\n");
	printf("      -n <instance cnt>\n");
	printf("              Number of concurrent processes\n");
	printf("\n");
	printf("      -u <usrcredsfile>\n");
	printf("              Alternate user credentials file\n");
	printf("\n");
	printf("      -a <appcredsfile>\n");
	printf("              Alternate app credentials file\n");
	printf("\n");
	printf("      -h      Print this help text and exit\n");
	printf("\n");
}

