#include "reddit.h"
#include "bcurl.h"
#include "bstr.h"
#include <time.h>
#include <errno.h>
#include "cJSON.h"
#include "cJSON_helper.h"
#include "blog.h"
#include <sys/types.h>
#include <sys/stat.h>


static bstr_t	*usern = NULL;
static bstr_t	*passw = NULL;
static bstr_t	*clientid = NULL;
static bstr_t	*clientsecr = NULL;

static bstr_t	*token = NULL;
static time_t	token_expire = 0;


static int
load_app_creds(const char *credsfilen)
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


static int
load_usr_creds(const char *credsfilen)
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
#define API_PREFIX		"https://oauth.reddit.com"


static int
check_token(void)
{
	int	err;
	bstr_t	*postdata;
	int	ret;
	bstr_t	*resp;
	cJSON	*json;
	int	expiresin;
	bstr_t	*url;
	bstr_t	*hdr;

	err = 0;
	postdata = NULL;
	resp = NULL;
	json = NULL;
	expiresin = 0;
	url = NULL;
	hdr = NULL;

	/* token_expire will be 0 on startup */
	if((token_expire != 0) &&
	    (time(NULL) + TOKEN_EXPIRE_MARGIN < token_expire))
		goto end_label;

	/* Token about to expire, get a new one. */

	blogf("Refreshing access token");

	bclear(token);

	url = binit();
	if(url == NULL) {
		blogf("Couldn't initialize url");
		err = ENOMEM;
		goto end_label;
	}

	bstrcat(url, TOKEN_URL);

	blogf("url=%s", bget(url));

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

	ret = bcurl_post_opts(bget(url), postdata, &resp,
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

	json = cJSON_Parse(bget(resp));
	if(json == NULL) {
		blogf("Couldn't parse response");
		err = EINVAL;
		goto end_label;
	}

	ret = cjson_get_childstr(json, "access_token", token);
	if(ret != 0) {
		blogf("JSON didn't contain access_token");
		err = ENOENT;
		goto end_label;
	}

	if(bstrempty(token)) {
		blogf("token is empty");
		err = EINVAL;
		goto end_label;
	}

	ret = cjson_get_childint(json, "expires_in", &expiresin);
	if(ret != 0) {
		blogf("JSON didn't contain expires_in");
		err = ENOENT;
		goto end_label;
	}

	if(expiresin <= 0) {
		blogf("invalide expires_in");
		err = EINVAL;
		goto end_label;
	}

	token_expire = time(NULL) + expiresin;

	hdr = binit();
	if(hdr == NULL) {
		blogf("Couldn't initialize hdr");
		err = ENOMEM;
		goto end_label;
	}

	/* bcurl will add the authorization header, to all subsequent
	 * requests it makes. */
	bprintf(hdr, "Authorization: bearer %s", bget(token));
	bcurl_header_add(bget(hdr));

	blogf("Access token refreshed, expires in %d sec", expiresin);

#if 0
	blogf("Token: %s", bget(token));
#endif


end_label:

	buninit(&postdata);
	buninit(&resp);
	buninit(&url);
	buninit(&hdr);

	if(json != NULL) {
		cJSON_Delete(json);
	}

	return err;
}



int
reddit_init(const char *usercredsfile, const char *appcredsfile,
	const char *http_useragent)
{
	int	ret;
	int	err;

	if(xstrempty(usercredsfile) || xstrempty(appcredsfile) ||
	    xstrempty(http_useragent))
		return EINVAL;

	err = 0;

	ret = bcurl_init();
	if(ret != 0) {
		blogf("Could not initialize curl: %s\n", strerror(ret));
		err = -1;
		goto end_label;
	}

	ret = bcurl_set_useragent(http_useragent);
	if(ret != 0) {
		blogf("Could not set user agent: %s\n", strerror(ret));
		err = -1;
		goto end_label;
	}

	ret = load_app_creds(appcredsfile);
	if(ret != 0) {
		blogf("Could not load app credentials.\n");
		err = -1;
		goto end_label;
	}

	ret = load_usr_creds(usercredsfile);
	if(ret != 0) {
		fprintf(stderr, "Could not load user credentials.\n");
		err = -1;
		goto end_label;
	}

	token = binit();
	if(token == NULL) {
		blogf("Couldn't initialize token");
		err = ENOMEM;
		goto end_label;
	}

	ret = check_token();
	if(ret != 0) {
		blogf("Could not authenticate with Reddit.\n");
		err = -1;
		goto end_label;
	}


end_label:

	if(err != 0) {
		bcurl_uninit();
		buninit(&token);
	}


	return err;
}


void
reddit_uninit(void)
{
	bcurl_uninit();
	buninit(&usern);
	buninit(&passw);
	buninit(&clientid);
	buninit(&clientsecr);
	buninit(&token);
}


int
reddit_get_new_comments(const char *subreddit, const char *postid,
	barr_t *comments, const int maxcnt)
{
	int			err;
	int			ret;
	bstr_t			*resp;
	cJSON			*json;
	bstr_t			*url;
	cJSON			*listing;
	cJSON			*listingdata;
	cJSON			*listingchildren;
	cJSON			*listingchild;
	cJSON			*listingchilddata;
	bstr_t			*val;
	cJSON			*stickied;
	reddit_comment_t	comment;
	int			addedcnt;

	err = 0;
	resp = NULL;
	json = NULL;
	url = NULL;
	val = NULL;
	addedcnt = 0;
	memset(&comment, 0, sizeof(reddit_comment_t));
	
	ret = check_token();
	if(ret != 0) {
		blogf("Token check failed");
		err = -1;
		goto end_label;
	}

	url = binit();
	if(url == NULL) {
		blogf("Couldn't initialize url");
		err = ENOMEM;
		goto end_label;
	}

	/* Why we call this with maxcnt + 1: reddit threads often have a
	 * sticky comment at the top and this is always returned by the
	 * API call. In case there's no sticky, it's still ok to request more
	 * comments than we need since we will bail out during parsing when
	 * we reach maxcnt. */
	bprintf(url, "%s/r/%s/comments/%s?sort=best&depth=1&limit=%d",
	    API_PREFIX, subreddit, postid, maxcnt + 1);


#if 0
	blogf("url=%s", bget(url));
#endif

	ret = bcurl_get(bget(url), &resp);
	if(ret != 0) {
		blogf("Couldn't make request");
		err = ret;
		goto end_label;
	}

	if(bstrempty(resp)) {
		blogf("Response is empty");
		err = EINVAL;
		goto end_label;
	}

	json = cJSON_Parse(bget(resp));
	if(json == NULL) {
		blogf("Couldn't parse JSON");
		err = ENOEXEC;
		goto end_label;
	}

	if(!cJSON_IsArray(json)) {
		blogf("Returned JSON is not an array");
		err = ENOEXEC;

	}

	val = binit();
	if(val == NULL) {
		blogf("Couldn't initialize val");
		err = ENOMEM;
		goto end_label;
	}


	for(listing = json->child; listing; listing = listing->next) {

		bclear(val);
		ret = cjson_get_childstr(listing, "kind", val);
		if(ret != 0 || bstrcmp(val, "Listing"))
			continue;

		listingdata = cJSON_GetObjectItemCaseSensitive(listing,
                    "data");
                if(listingdata == NULL) {
			blogf("Listing didn't contain data");
			continue;
		}

		listingchildren = cJSON_GetObjectItemCaseSensitive(listingdata,
                    "children");
		if(listingchildren == NULL ||
		    !cJSON_IsArray(listingchildren)) {
			blogf("Listing data didn't contain \"children\" array");
				continue;
		}
		
		for(listingchild = listingchildren->child; listingchild;
		    listingchild = listingchild->next) {
			bclear(val);
			ret = cjson_get_childstr(listingchild, "kind", val);
			/* On Reddit, a t1 type is a comment */
			if(ret != 0 || bstrcmp(val, "t1"))
				continue;

			listingchilddata = cJSON_GetObjectItemCaseSensitive(
			    listingchild, "data");
			if(listingchilddata == NULL ||
			    !cJSON_IsObject(listingchilddata)) {
				blogf("Listing child element didn't contain"
				    " \"data\" object");
				continue;
			}
				
			/* Ignore stickied comments */
			stickied = cJSON_GetObjectItemCaseSensitive(
			    listingchilddata, "stickied");
			if(stickied != NULL && cJSON_IsTrue(stickied))
				continue;


			comment.rc_id = binit();
			if(comment.rc_id == NULL) {
				blogf("Can't allocate comment.rc_id");
				err = ENOMEM;
				goto end_label;
			}

			ret = cjson_get_childstr(listingchilddata, "name",
			    comment.rc_id);
			if(ret != 0) {
				blogf("Comment had no \"name\"");
				err = ENOENT;
				goto end_label;
			} 

			comment.rc_author = binit();
			if(comment.rc_author == NULL) {
				blogf("Can't allocate comment.rc_author");
				err = ENOMEM;
				goto end_label;
			}

			ret = cjson_get_childstr(listingchilddata, "author",
			    comment.rc_author);
			if(ret != 0) {
				blogf("Comment had no author");
				err = ENOENT;
				goto end_label;
			} 

			comment.rc_body = binit();
			if(comment.rc_body == NULL) {
				blogf("Can't allocate comment.rc_body");
				err = ENOMEM;
				goto end_label;
			}
			ret = cjson_get_childstr(listingchilddata, "body",
			    comment.rc_body);
			if(ret != 0) {
				blogf("Comment had no body");
				err = ENOENT;
				goto end_label;
			}

blogf("body: %s", bget(comment.rc_body));

			barr_add(comments, &comment);
			memset(&comment, 0, sizeof(reddit_comment_t));

			++addedcnt;

			if(addedcnt >= maxcnt)
				break;

		}
	
		if(addedcnt >= maxcnt)
			break;
	}


#if 0
	btofile("out.json", resp);
#endif


end_label:

	buninit(&resp);
	buninit(&url);
	buninit(&val);

	if(json != NULL) {
		cJSON_Delete(json);
	}

	if(comment.rc_id)
		buninit(&comment.rc_id);
	if(comment.rc_author)
		buninit(&comment.rc_author);
	if(comment.rc_body)
		buninit(&comment.rc_body);

	return err;
}



