#ifndef REDDIT_H
#define REDDIT_H


int reddit_init(const char *, const char *, const char *);

int reddit_get_new_comments(void);

void reddit_uninit(void);

#endif


