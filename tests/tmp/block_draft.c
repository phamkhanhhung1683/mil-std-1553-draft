#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* ================= CONFIG ================= */
#define BUF_SIZE 64
#define PAYLOAD_SIZE 60

/* ================= BLOCK ================= */
/* dùng bit flag để FIRST & LAST có thể cùng tồn tại */
enum {
    BLOCK_FIRST = 1 << 0,
    BLOCK_LAST  = 1 << 1
};

struct block {
    uint8_t  type;
    uint8_t  msg_id;
    uint16_t total_len;     /* chỉ meaningful khi FIRST */
    uint8_t  payload[PAYLOAD_SIZE];
};

/* ================= QUEUE ================= */
struct node {
    char buf[BUF_SIZE];
    struct node *next;
};

struct ts_buf_queue {
    struct node *head;
    struct node *tail;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
};

void ts_buf_queue_init(struct ts_buf_queue *q) {
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

void ts_buf_queue_push(struct ts_buf_queue *q, const char buf[BUF_SIZE]) {
    struct node *n = malloc(sizeof(*n));
    memcpy(n->buf, buf, BUF_SIZE);
    n->next = NULL;

    pthread_mutex_lock(&q->mutex);
    if (!q->tail)
        q->head = n;
    else
        q->tail->next = n;
    q->tail = n;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

void ts_buf_queue_pop(struct ts_buf_queue *q, char buf[BUF_SIZE]) {
    pthread_mutex_lock(&q->mutex);
    while (!q->head)
        pthread_cond_wait(&q->not_empty, &q->mutex);

    struct node *n = q->head;
    memcpy(buf, n->buf, BUF_SIZE);
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    pthread_mutex_unlock(&q->mutex);
    free(n);
}

/* ================= PRODUCER ================= */
void send_message(struct ts_buf_queue *q,
                  const uint8_t *msg,
                  uint16_t msg_len,
                  uint8_t msg_id)
{
    uint16_t offset = 0;

    while (offset < msg_len) {
        uint8_t buf[BUF_SIZE] = {0};
        struct block *b = (struct block *)buf;

        uint16_t remain = msg_len - offset;
        uint16_t copy_len =
            (remain > PAYLOAD_SIZE) ? PAYLOAD_SIZE : remain;

        /* SET TYPE (CHUẨN) */
        if (offset == 0 && remain <= PAYLOAD_SIZE)
            b->type = BLOCK_FIRST | BLOCK_LAST;   /* single block */
        else if (offset == 0)
            b->type = BLOCK_FIRST;
        else if (remain <= PAYLOAD_SIZE)
            b->type = BLOCK_LAST;
        else
            b->type = 0;

        b->msg_id = msg_id;
        b->total_len = (b->type & BLOCK_FIRST) ? msg_len : 0;

        memcpy(b->payload, msg + offset, copy_len);
        offset += copy_len;

        ts_buf_queue_push(q, (char *)buf);
    }
}

/* ================= CONSUMER ================= */
void *consumer_thread(void *arg) {
    struct ts_buf_queue *q = arg;

    uint8_t *msg_buf = NULL;
    uint16_t msg_len = 0;
    uint16_t received = 0;
    uint8_t current_msg_id = 0;

    while (1) {
        uint8_t buf[BUF_SIZE];
        ts_buf_queue_pop(q, (char *)buf);
        struct block *b = (struct block *)buf;

        /* INIT KHI FIRST */
        if (b->type & BLOCK_FIRST) {
            msg_len = b->total_len;
            received = 0;
            current_msg_id = b->msg_id;

            msg_buf = malloc(msg_len);
            if (!msg_buf) {
                printf("malloc failed\n");
                continue;
            }
        }

        uint16_t copy_len =
            (msg_len - received > PAYLOAD_SIZE)
            ? PAYLOAD_SIZE
            : (msg_len - received);

        memcpy(msg_buf + received, b->payload, copy_len);
        received += copy_len;

        /* KẾT THÚC KHI LAST */
        if (b->type & BLOCK_LAST) {
            printf("Nhan du message %d (%d bytes):\n",
                   current_msg_id, msg_len);

            for (int i = 0; i < msg_len; i++)
                printf("%02X ", msg_buf[i]);
            printf("\n\n");

            free(msg_buf);
            msg_buf = NULL;
        }
    }
    return NULL;
}

/* ================= PRODUCER THREAD ================= */
void *producer_thread(void *arg) {
    struct ts_buf_queue *q = arg;

    uint8_t msg_small[] = {1,2,3,4,5};
    uint8_t msg_medium[80];
    uint8_t msg_large[200];

    for (int i = 0; i < 80; i++) msg_medium[i] = i;
    for (int i = 0; i < 200; i++) msg_large[i] = i + 1;

    send_message(q, msg_small, sizeof(msg_small), 1);
    // sleep(1);
    send_message(q, msg_medium, sizeof(msg_medium), 2);
    // sleep(1);
    send_message(q, msg_large, sizeof(msg_large), 3);

    return NULL;
}

/* ================= MAIN ================= */
int main() {
    pthread_t prod, cons;
    struct ts_buf_queue queue;

    ts_buf_queue_init(&queue);

    pthread_create(&cons, NULL, consumer_thread, &queue);
    pthread_create(&prod, NULL, producer_thread, &queue);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);
    return 0;
}
