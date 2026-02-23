#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "thread_safe_buf_queue.h"

#define BUF_SIZE 64

struct node {
    char buf[BUF_SIZE];
    struct node *next;
};

void thread_safe_buf_queue_init(struct thread_safe_buf_queue *q) {
    q->head = NULL;
    q->tail = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

void thread_safe_buf_queue_destroy(struct thread_safe_buf_queue *q) {
    if (!q)
        return;

    pthread_mutex_lock(&q->mutex);

    struct node *current = q->head;
    while (current != NULL) {
        struct node *next = current->next;
        free(current);
        current = next;
    }

    q->head = NULL;
    q->tail = NULL;

    pthread_mutex_unlock(&q->mutex);

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
}

int thread_safe_buf_queue_empty(struct thread_safe_buf_queue *q) {
	int ret;

    pthread_mutex_lock(&q->mutex);
    ret = (q->head == NULL) ? 1 : 0;
    pthread_mutex_unlock(&q->mutex);

	return ret;
}

int thread_safe_buf_queue_push(struct thread_safe_buf_queue *q, const char buf[BUF_SIZE]) {
    struct node *node = malloc(sizeof(*node));
    if (!node) {
        perror("thread_safe_buf_queue_push malloc");
        return -1;
    }

    memcpy(node->buf, buf, BUF_SIZE);
    node->next = NULL;

    pthread_mutex_lock(&q->mutex);

    if (!q->tail) {
        q->head = node;
    } else {
        q->tail->next = node;
    }

    q->tail = node;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);

    return 0;
}

void thread_safe_buf_queue_pop(struct thread_safe_buf_queue *q, char buf[BUF_SIZE]) {
    struct node *node;

    pthread_mutex_lock(&q->mutex);

    while (q->head == NULL) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    node = q->head;
    memcpy(buf, node->buf, BUF_SIZE);

    q->head = node->next;
    if (!q->head) {
        q->tail = NULL;
    }

    pthread_mutex_unlock(&q->mutex);

    free(node);
}
