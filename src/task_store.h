#ifndef TASK_STORE_H
#define TASK_STORE_H

#include <pthread.h>
#include <time.h>

#define TASK_STORE_MAX 64
#define TASK_TITLE_CAP 128

typedef struct {
    char title[TASK_TITLE_CAP];
    int completed;
    time_t due; /* local midnight of the due date; only meaningful if has_due */
    int has_due;
} gtask_item_t;

typedef struct {
    pthread_mutex_t lock;
    gtask_item_t tasks[TASK_STORE_MAX];
    int count;
    time_t last_success;
    int last_fetch_ok;
} task_store_t;

void task_store_init(task_store_t *s);
void task_store_destroy(task_store_t *s);

/* Replaces the stored task list wholesale. count is clamped to
 * TASK_STORE_MAX. */
void task_store_replace(task_store_t *s, const gtask_item_t *tasks, int count);

void task_store_set_fetch_status(task_store_t *s, int ok);

/* Copies up to max tasks into out. Returns the number copied. */
int task_store_snapshot(task_store_t *s, gtask_item_t *out, int max);

#endif
