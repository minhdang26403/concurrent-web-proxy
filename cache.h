#ifndef __CACHE_H__
#define __CACHE_H__
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Global variables for the web cache*/
typedef struct cache_obj {
  struct cache_obj *prev;
  struct cache_obj *next;
  char *tag;
  char *buf;
  size_t size;
} cache_object;

/* Helper functions for the web cache */
void init_cache();
cache_object *find_cache_object(char *url);
void insert_obj(char *url, char *buf, size_t obj_size);
void free_cache();

#endif