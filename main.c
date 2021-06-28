#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "bstr.h"
#include "blog.h"
#include "bcurl.h"

void usage(const char *);

#if 0
int
main(int argc, char **argv)
{
	char	*execn;
	bstr_t	*val;
	int	err;
	int	ret;
	bstr_t	*filen;
	bstr_t	*cmd;
	bstr_t	*newval;
	barr_t	*elems;
	bstr_t	*elem;
	char	*listn;
	bstr_t	*key;
	bstr_t	*tmpkey;
	int	c;
	int	docreate;
	barr_t	*newelems;

	val = NULL;
	err = 0;
	filen = NULL;
	key = NULL;
	tmpkey = NULL;
	cmd = NULL;
	newval = NULL;
	elems = NULL;
	newelems = NULL;

	execn = basename(argv[0]);
	if(xstrempty(execn)) {
		fprintf(stderr, "Can't get executable name\n");
		err = -1;
		goto end_label;
	}

	docreate = 0;
	opterr = 0;

	while ((c = getopt (argc, argv, "c")) != -1) {
		switch (c) {
		case 'c':
			++docreate;
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

	if(argc != optind + 1 || xstrempty(argv[optind])) {
		usage(execn);
		err = -1;
		goto end_label;
	}

	listn = argv[optind];
	

	ret = hiredis_init();
	if(ret != 0) {
		fprintf(stderr, "Could not connect to redis\n");
		err = -1;
		goto end_label;
	}

	key = binit();
	if(key == NULL) {
		fprintf(stderr, "Can't allocate key\n");
		err = -1;
		goto end_label;
	}
	bprintf(key, "%s%s", KEY_PREF, listn);

	elems = barr_init(sizeof(bstr_t));
	if(elems == NULL) {
		fprintf(stderr, "Couldn't allocate elems\n");
		err = -1;
		goto end_label;
	}

	val = binit();
	if(val == NULL) {
		fprintf(stderr, "Can't allocate val\n");
		err = -1;
		goto end_label;
	}

	ret = hiredis_lrange(bget(key), 0, - 1, elems);
	if(ret != 0) {
		fprintf(stderr, "Couldn't lrange: %s\n", strerror(ret));
		err = -1;
		goto end_label;
	}

	if(barr_cnt(elems) == 0 && !docreate) {
		fprintf(stderr, "Key doesn't exist\n");
		err = -1;
		goto end_label;
	}

	for(elem = (bstr_t *) barr_begin(elems);
	    elem < (bstr_t *) barr_end(elems); ++elem) {
		bprintf(val, "%s\n", bget(elem));
	}

	filen = binit();
	if(filen == NULL) {
		fprintf(stderr, "Can't allocate filen\n");
		err = -1;
		goto end_label;
	}
	bprintf(filen, "/tmp/%s_%d_%d", execn, getpid(), time(NULL));
	
	if(!bstrempty(val)) { /* OK to be empty if we are creating */
		ret = btofile(bget(filen), val);
		if(ret != 0) {
			fprintf(stderr, "Couldn't write value to filen\n");
			err = -1;
			goto end_label;
		}
	}

	cmd = binit();
	if(cmd == NULL) {
		fprintf(stderr, "Can't allocate cmd\n");
		err = -1;
		goto end_label;
	}
	bprintf(cmd, "%s %s", EDITOR, bget(filen));

	ret = system(bget(cmd));
	if(ret != 0) {
		fprintf(stderr, "Couldn't execute system command: %d\n", ret);
		err = -1;
		goto end_label;
	}

	newval = binit();
	if(newval == NULL) {
		fprintf(stderr, "Can't allocate newval\n");
		err = -1;
		goto end_label;
	}

	ret = bfromfile(newval, bget(filen));
	if(ret != 0 && !docreate) {
		/* OK for file to not exist if we are creating. Just means
		 * "unchanged". */
		fprintf(stderr, "Couldn't load file\n");
		err = -1;
		goto end_label;
	}

	if(!bstrcmp(val, bget(newval))) {
		if(docreate && bstrempty(newval))
			printf("List not created.\n");
		else
			printf("List not changed.\n");
		goto end_label;
	}

	ret = bstrsplit(newval, "\n", 0, &newelems);
	if(ret != 0 || newelems == NULL) {
		fprintf(stderr, "Couldn't split new value\n");
		err = -1;
		goto end_label;
	}
	if(barr_cnt(newelems) == 0) {
		fprintf(stderr, "New list can't be empty, use -d to delete"
		    " a list\n");
		err = -1;
		goto end_label;
	}

	tmpkey = binit();
	if(tmpkey == NULL) {
		fprintf(stderr, "Can't allocate tmpkey\n");
		err = -1;
		goto end_label;
	}
	bprintf(tmpkey, "%s_tmp_%s_%d_%d", KEY_PREF, execn, getpid(),
	    time(NULL));

	for(elem = (bstr_t *) barr_begin(newelems);
	    elem < (bstr_t *) barr_end(newelems); ++elem) {
		if(bstrempty(elem))
			continue;
		ret = hiredis_rpush(bget(tmpkey), bget(elem));
		if(ret != 0) {
			fprintf(stderr, "Could not rpush element.\n");
			err = -1;
			goto end_label;
		}
	}

	ret = hiredis_rename(bget(tmpkey), bget(key));
	if(ret != 0) {
		fprintf(stderr, "Could not rename.\n");
		err = -1;
		goto end_label;
	}

	printf("List updated successfully.\n");


end_label:
	
	buninit(&val);

	if(!bstrempty(filen)) {
		(void) unlink(bget(filen));
	}
	buninit(&filen);
	buninit(&cmd);
	buninit(&newval);
	buninit(&key);

	if(err != 0 && !bstrempty(tmpkey)) {
		(void) hiredis_del(bget(tmpkey), NULL);
	}
	buninit(&tmpkey);

	if(elems) {
		for(elem = (bstr_t *) barr_begin(elems);
		    elem < (bstr_t *) barr_end(elems); ++elem) {
			buninit_(elem);
		}
		barr_uninit(&elems);
	}
	if(newelems) {
		for(elem = (bstr_t *) barr_begin(newelems);
		    elem < (bstr_t *) barr_end(newelems); ++elem) {
			buninit_(elem);
		}
		barr_uninit(&newelems);
	}

	hiredis_uninit();

	return err;

}
#endif


#define MODE_NONE	0
#define MODE_LIST	1
#define MODE_TAIL	2

int
main(int argc, char **argv)
{
	char	*execn;
	int	err;
	int	ret;
	int	mode;
	int	c;
	char	*usern;
	char	*passw;
	char	*subredditn;
	char	*postid;
	int	delaysec;


	err = 0;
	mode = MODE_NONE;
	delaysec = 0;

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


	while ((c = getopt (argc, argv, "hu:p:d:l:t:")) != -1) {
		switch (c) {
		case 'h':
			usage(execn);
			goto end_label;
		case 'u':
			usern = optarg;
			if(xstrempty(usern)) {
				fprintf(stderr,
				    "Invalid username.\n");
				err = -1;
				goto end_label;
			}
			break;

		case 'p':
			passw = optarg;
			if(xstrempty(passw)) {
				fprintf(stderr,
				    "Invalid password.\n");
				err = -1;
				goto end_label;
			}
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
	printf("      %s -u <user> -p <pass> -l <subreddit (sans '/r/' prefix)>\n", execn);
	printf("\n");
 	printf("  Tail live comments on post:\n");
	printf("      %s -u <user> -p <pass> [-d delaysec] -t <postid>\n", execn);
	printf("\n");
 	printf("  Replay comments on post:\n");
	printf("      %s -u <user> -p <pass> -t <time> -r <postid>\n", execn);
	printf("\n");
}
