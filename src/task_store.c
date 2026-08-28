#include "task_store.h"
#include <string.h>

void task_store_init(task_store_t *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->lock, NULL);
}

void task_store_destroy(task_store_t *s) {
    pthread_mutex_destroy(&s->lock);
}

void task_store_replace(task_store_t *s, const gtask_item_t *tasks, int count) {
    if (count > TASK_STORE_MAX) count = TASK_STORE_MAX;
    pthread_mutex_lock(&s->lock);
    memcpy(s->tasks, tasks, (size_t)count * sizeof(gtask_item_t));
    s->count = count;
    s->last_success = time(NULL);
    pthread_mutex_unlock(&s->lock);
}

void task_store_set_fetch_status(task_store_t *s, int ok) {
    pthread_mutex_lock(&s->lock);
    s->last_fetch_ok = ok;
    pthread_mutex_unlock(&s->lock);
}

int task_store_snapshot(task_store_t *s, gtask_item_t *out, int max) {
    pthread_mutex_lock(&s->lock);
    int n = s->count < max ? s->count : max;
    memcpy(out, s->tasks, (size_t)n * sizeof(gtask_item_t));
    pthread_mutex_unlock(&s->lock);
    return n;
}
