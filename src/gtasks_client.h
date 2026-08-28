#ifndef GTASKS_CLIENT_H
#define GTASKS_CLIENT_H

#include "task_store.h"

/* Fetches the default Google Tasks list's incomplete tasks into out (up
 * to max_out). Returns the count fetched, or -1 on any
 * transport/HTTP/parse error -- on -1 the caller must NOT clear whatever
 * tasks it already has cached. */
int gtasks_fetch_tasks(const char *access_token, gtask_item_t *out, int max_out);

#endif
