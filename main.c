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
#include "cJSON.h"
#include "cJSON_helper.h"


#define DEFAULT_USERCREDSFILE	".rlctail_usercreds.json"
#define DEFAULT_APPCREDSFILE	".rlctail_appcreds.json"
#define HTTP_USERAGENT		"macos:rlctail (by /u/brewbake)"

#define MODE_NONE	0
#define MODE_LIST	1
#define MODE_TAIL	2

bstr_t *usern = NULL;
bstr_t *passw = NULL;
bstr_t *clientid = NULL;
bstr_t *clientsecr = NULL;

bstr_t *token = NULL;
time_t token_expire = 0;

void usage(const char *);

int loadusrcreds(const char *);
int loadappcreds(const char *);
int checktoken(void);


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
	char	*usrcredsfile;
	char	*appcredsfile;
	
	err = 0;
	mode = MODE_NONE;
	delaysec = 0;
	usrcredsfile = DEFAULT_USERCREDSFILE;
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


	ret = bcurl_init();
	if(ret != 0) {
		fprintf(stderr, "Could not initialize curl: %s\n",
		    strerror(ret));
		err = -1;
		goto end_label;
	}

	ret = bcurl_set_useragent(HTTP_USERAGENT);	
	if(ret != 0) {
		fprintf(stderr, "Could not set user agent: %s\n",
		    strerror(ret));
		err = -1;
		goto end_label;
	}

	if(argc < 2) {
		usage(execn);
		err = -1;
		goto end_label;
	}


	while ((c = getopt (argc, argv, "ha:u:d:l:t:")) != -1) {
		switch (c) {
		case 'h':
			usage(execn);
			goto end_label;

		case 'a':
			appcredsfile = optarg;
			break;

		case 'u':
			usrcredsfile = optarg;
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

	if(xstrempty(appcredsfile)) {
		fprintf (stderr, "Invalid app credentials file specified.\n");
		err = -1;
		goto end_label;
	}

	ret = loadappcreds(appcredsfile);
	if(ret != 0) {
		fprintf (stderr, "Could not load app credentials.\n");
		err = -1;
		goto end_label;
	}

	if(xstrempty(usrcredsfile)) {
		fprintf (stderr, "Invalid user credentials file specified.\n");
		err = -1;
		goto end_label;
	}

	ret = loadusrcreds(usrcredsfile);
	if(ret != 0) {
		fprintf (stderr, "Could not load user credentials.\n");
		err = -1;
		goto end_label;
	} 

	ret = checktoken();
	if(ret != 0) {
		fprintf (stderr, "Could not authenticate with Reddit.\n");
		err = -1;
		goto end_label;
	} 


end_label:

	bcurl_uninit();

	blog_uninit();

	buninit(&usern);
	buninit(&passw);

	return err;
	
}




void
usage(const char *execn)
{
	printf("usage:\n");
	printf("\n");
 	printf("  List current match threads:\n");
	printf("      %s -l <subreddit (sans '/r/'"
	    " prefix)>\n", execn);
	printf("\n");
 	printf("  Tail live comments on post:\n");
	printf("      %s [-d delaysec] -t <postid>\n", execn);
	printf("\n");
 	printf("  Replay comments on post:\n");
	printf("      %s -t <time> -r <postid>\n", execn);
	printf("\n");
	printf("  For all invocations, alternate credential files can be specified via:\n");
	printf("      [-u <usrcredsfile>]\n");
	printf("      [-a <appcredsfile>]\n");
	printf("\n");
}


int
loadappcreds(const char *credsfilen)
{
	int		ret;
	struct stat	st;
	int		err;
	bstr_t		*filecont;
	cJSON		*json;

	err = 0;
	filecont = NULL;
	memset(&st, 0, sizeof(struct stat));
	json = NULL;

	if(xstrempty(credsfilen)) {
		err = EINVAL;
		goto end_label;
	}

	ret = stat(credsfilen, &st);
	if(ret != 0) {
		blogf("Could not stat creds file: %s", strerror(ret));
		err = ret;
		goto end_label;
	}

	if(!(st.st_mode & S_IFREG)) {
		blogf("Creds file is not a regular file");
		err = EINVAL;
		goto end_label;
	}

	if((st.st_mode & (S_IRUSR | S_IWUSR)) != (S_IRUSR | S_IWUSR)) {
		blogf("Permissions for creds file not correct, must be 600");
		err = EINVAL;
		goto end_label;
	}

	if(st.st_mode & (S_IXUSR | S_IRWXG | S_IRWXO)) {
		blogf("Permissions for creds file not correct, must be 600");
		err = EINVAL;
		goto end_label;
	}

	filecont = binit();
	if(filecont == NULL) {
		blogf("Couldn't initialize filecont");
		err = ENOMEM;
		goto end_label;
	}

	clientid = binit();
	if(clientid == NULL) {
		blogf("Couldn't initialize clientid");
		err = ENOMEM;
		goto end_label;
	}

	clientsecr = binit();
	if(clientsecr == NULL) {
		blogf("Couldn't initialize clientsecr");
		err = ENOMEM;
		goto end_label;
	}

	ret = bfromfile(filecont, credsfilen);
	if(ret != 0) {
		blogf("Couldn't load creds file");
		err = ret;
		goto end_label;
	}

	json = cJSON_Parse(bget(filecont));
	if(json == NULL) {
		blogf("Couldn't parse JSON");
		err = ENOEXEC;
		goto end_label;
	}

	ret = cjson_get_childstr(json, "clientid", clientid);
	if(ret != 0) {
		blogf("JSON didn't contain clientid");
		err = ENOENT;
		goto end_label;
	}
	
	ret = cjson_get_childstr(json, "clientsecret", clientsecr);
	if(ret != 0) {
		blogf("JSON didn't contain clientsecret");
		err = ENOENT;
		goto end_label;
	}
	

end_label:

	if(json != NULL) {
		cJSON_Delete(json);
	}

	buninit(&filecont);

	return err;

}


int
loadusrcreds(const char *credsfilen)
{
	int		ret;
	struct stat	st;
	int		err;
	bstr_t		*filecont;
	cJSON		*json;

	err = 0;
	filecont = NULL;
	memset(&st, 0, sizeof(struct stat));
	json = NULL;

	if(xstrempty(credsfilen)) {
		err = EINVAL;
		goto end_label;
	}

	ret = stat(credsfilen, &st);
	if(ret != 0) {
		blogf("Could not stat creds file: %s", strerror(ret));
		err = ret;
		goto end_label;
	}

	if(!(st.st_mode & S_IFREG)) {
		blogf("Creds file is not a regular file");
		err = EINVAL;
		goto end_label;
	}

	if((st.st_mode & (S_IRUSR | S_IWUSR)) != (S_IRUSR | S_IWUSR)) {
		blogf("Permissions for creds file not correct, must be 600");
		err = EINVAL;
		goto end_label;
	}

	if(st.st_mode & (S_IXUSR | S_IRWXG | S_IRWXO)) {
		blogf("Permissions for creds file not correct, must be 600");
		err = EINVAL;
		goto end_label;
	}

	
	filecont = binit();
	if(filecont == NULL) {
		blogf("Couldn't initialize filecont");
		err = ENOMEM;
		goto end_label;
	}

	usern = binit();
	if(usern == NULL) {
		blogf("Couldn't initialize usern");
		err = ENOMEM;
		goto end_label;
	}

	passw = binit();
	if(passw == NULL) {
		blogf("Couldn't initialize passw");
		err = ENOMEM;
		goto end_label;
	}

	ret = bfromfile(filecont, credsfilen);
	if(ret != 0) {
		blogf("Couldn't load creds file");
		err = ret;
		goto end_label;
	}

	json = cJSON_Parse(bget(filecont));
	if(json == NULL) {
		blogf("Couldn't parse JSON");
		err = ENOEXEC;
		goto end_label;
	}

	ret = cjson_get_childstr(json, "username", usern);
	if(ret != 0) {
		blogf("JSON didn't contain username");
		err = ENOENT;
		goto end_label;
	}
	
	ret = cjson_get_childstr(json, "password", passw);
	if(ret != 0) {
		blogf("JSON didn't contain passw");
		err = ENOENT;
		goto end_label;
	}


	
	

end_label:

	if(json != NULL) {
		cJSON_Delete(json);
	}

	buninit(&filecont);

	return err;
}


#define TOKEN_EXPIRE_MARGIN	600	/* Refresh 10 minutes before expiry */
#define TOKEN_URL		"https://www.reddit.com/api/v1/access_token"

int
checktoken(void)
{
	int	err;
	bstr_t	*postdata;
	int	ret;
	bstr_t	*resp;

	err = 0;
	postdata = NULL;
	resp = NULL;

	/* token_expire will be 0 on startup */
	if((token_expire != 0) &&
	    (time(NULL) + TOKEN_EXPIRE_MARGIN < token_expire))
		goto end_label;


	/* Token about to expire, get a new one. */
	postdata = binit();
	if(postdata == NULL) {
		blogf("Couldn't initialize postdata");
		err = ENOMEM;
		goto end_label;
	}

	ret = bstrcat_urlenc_field(postdata, "grant_type", "password");
	if(ret != 0) {
		blogf("Couldn't add grant type to postdata");
		err = ret;
		goto end_label;
	}

	ret = bstrcat_urlenc_field(postdata, "username", bget(usern));
	if(ret != 0) {
		blogf("Couldn't add user name to postdata");
		err = ret;
		goto end_label;
	}

	ret = bstrcat_urlenc_field(postdata, "password", bget(passw));
	if(ret != 0) {
		blogf("Couldn't add password to postdata");
		err = ret;
		goto end_label;
	}

	ret = bcurl_post_opts(TOKEN_URL, postdata, &resp,
	    bget(clientid), bget(clientsecr));
	if(ret != 0) {
		blogf("Couldn't request new token");
		err = ret;
		goto end_label;
	}

	if(bstrempty(resp)) {
		blogf("Response is empty");
		err = EINVAL;
		goto end_label;
	}
	

	printf("=====\n%s\n=====\n", bget(resp));

	


end_label:

	buninit(&postdata);
	buninit(&resp);
	return err;
}

