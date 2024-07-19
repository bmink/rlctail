#include "cJSON_helper.h"
#include <errno.h>


int
cjson_get_childstr(cJSON *json, const char *name, bstr_t *str)
{
	cJSON	*child;
	
	if(json == NULL || xstrempty(name) || str == NULL)
		return EINVAL;

	child = cJSON_GetObjectItemCaseSensitive(json, name);
	if(child == NULL)
		return ENOENT;

	if(!cJSON_IsString(child))
		return ENOEXEC;

	if(child->valuestring == NULL)
		return ENOEXEC;

	bstrcat(str, child->valuestring);

	return 0;
}


int
cjson_get_childint(cJSON *json, const char *name, int *val)
{
	cJSON	*child;
	
	if(json == NULL || xstrempty(name) || val == NULL)
		return EINVAL;

	child = cJSON_GetObjectItemCaseSensitive(json, name);
	if(child == NULL)
		return ENOENT;

	if(!cJSON_IsNumber(child))
		return ENOEXEC;

	*val = child->valueint;

	return 0;
}
