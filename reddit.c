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

	err = 0;
	postdata = NULL;
	resp = NULL;
	json = NULL;
	expiresin = 0;
	url = NULL;

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

	blogf("Access token refreshed, expires in %d sec", expiresin);


{
	bclear(url);
	bclear(resp);
	bprintf(url, "%s/r/soccer/comments/1je6e68?sort=best&depth=1&limit=10&after=t1_mig06tv", API_PREFIX);
	bstr_t *hdr = binit();
	bprintf(hdr, "Authorization: bearer %s", bget(token));
	bcurl_header_add(bget(hdr));

	blogf("%s", bget(url));
	blogf("%s", bget(hdr));

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

	blogf("%s", bget(resp));

	btofile("out.json", resp);

	buninit(&hdr);

}



end_label:

	buninit(&postdata);
	buninit(&resp);
	buninit(&url);

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


