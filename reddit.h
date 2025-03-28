#ifndef REDDIT_H
#define REDDIT_H

#include "bstr.h"
#include "barr.h"
#include <time.h>


typedef struct reddit_comment {
	bstr_t	*rc_id;
	bstr_t	*rc_author;
	bstr_t	*rc_body;
	time_t	rc_retrieved;
} reddit_comment_t;



int reddit_init(const char *, const char *, const char *);

int reddit_get_new_comments(const char *, const char *, barr_t *, const int);

void reddit_uninit(void);

#endif


