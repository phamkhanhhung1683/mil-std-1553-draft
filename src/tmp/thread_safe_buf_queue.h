#ifndef _THREAD_SAFE_BUF_QUEUE_H
#define _THREAD_SAFE_BUF_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pthread.h>

struct thread_safe_buf_queue {
    uint8_t *storage;
    uint32_t head;
    uint32_t tail;

    pthread_mutex_t mutex;
};

bool thread_safe_buf_queue_init(struct thread_safe_buf_queue *q);
void thread_safe_buf_queue_destroy(struct thread_safe_buf_queue *q);

int thread_safe_buf_queue_push(struct thread_safe_buf_queue *q, const void *buf, size_t size);
int thread_safe_buf_queue_pop(struct thread_safe_buf_queue *q, void *buf, size_t size);

#endif  /* _THREAD_SAFE_BUF_QUEUE_H */
