#ifndef CJSON_HELPER_H
#define CJSON_HELPER_H

#include "bstr.h"
#include "cJSON.h"

int cjson_get_childstr(cJSON *, const char *, bstr_t *);
int cjson_get_childint(cJSON *, const char *, int *);

#endif

