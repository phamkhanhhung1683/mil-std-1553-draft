#ifndef _THREAD_SAFE_BUF_QUEUE_H
#define _THREAD_SAFE_BUF_QUEUE_H

#include <pthread.h>

struct thread_safe_buf_queue {
    struct node *head;
    struct node *tail;

    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
};

void thread_safe_buf_queue_init(struct thread_safe_buf_queue *q);
void thread_safe_buf_queue_destroy(struct thread_safe_buf_queue *q);
int thread_safe_buf_queue_empty(struct thread_safe_buf_queue *q);
int thread_safe_buf_queue_push(struct thread_safe_buf_queue *q, const char buf[64]);
void thread_safe_buf_queue_pop(struct thread_safe_buf_queue *q, char buf[64]);

#endif /* _THREAD_SAFE_BUF_QUEUE_H */