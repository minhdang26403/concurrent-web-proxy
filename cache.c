#include "cache.h"

/* Cache is implemented using a doubly-linked list */
static size_t remain_cache_size;
static cache_object *head;
static cache_object *tail;

sem_t mutex_update; /* Protect updating the cache when reading it */

/* Private helper functions */
static void move_obj_to_head(cache_object *obj);
static void evict_obj();

void init_cache() {
  head = NULL;
  tail = NULL;
  remain_cache_size = MAX_CACHE_SIZE;
  Sem_init(&mutex_update, 0, 1);
}

cache_object *find_cache_object(char *url) {   
  cache_object *cur = head;
  while (cur != NULL) {
    if (!strcmp(cur->tag, url)) {
      P(&mutex_update);
      move_obj_to_head(cur);
      V(&mutex_update);
      return cur;
    }
    cur = cur->next;
  }
  return NULL;
}

static void move_obj_to_head(cache_object *obj) {   
  if (head == obj)
    return;
  obj->prev->next = obj->next;
  if (obj->next != NULL) {
    obj->next->prev = obj->prev;
  } else {
    tail = obj->prev;
  }
  obj->next = head;
  obj->prev = NULL;
  head->prev = obj;
  head = obj;
  return;
}

void insert_obj(char *url, char *buf, size_t obj_size) {
  while (obj_size > remain_cache_size) {
    evict_obj();
  }

  cache_object *new_obj = Malloc(sizeof(cache_object));

  /* Set tag for the cache block */
  new_obj->tag = Malloc(strlen(url));
  strcpy(new_obj->tag, url);

  /* Cache object content */
  new_obj->buf = Malloc(obj_size);
  memcpy(new_obj->buf, buf, obj_size);

  /* Update cache size */
  remain_cache_size -= obj_size;
  new_obj->size = obj_size;

  /* Insert object to the front */
  new_obj->next = head;
  new_obj->prev = NULL;
  if (tail == NULL)
    tail = new_obj;
  else
    head->prev = new_obj;
  head = new_obj;
}

static void evict_obj() {   
  remain_cache_size += tail->size;
  tail = tail->prev;
  Free(tail->next);
  tail->next = NULL;
  return;
}

void free_cache() {
  cache_object *cur_block = head, *next_block;
  while (cur_block != NULL) {
    next_block = cur_block->next;
    Free(cur_block->buf);
    Free(cur_block->tag);
    Free(cur_block);
    cur_block = next_block;
  }
}