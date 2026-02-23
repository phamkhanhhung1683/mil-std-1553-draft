#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <pthread.h>

#include "thread_safe_buf_queue.h"

#define BLOCK_SIZE 64
#define PAYLOAD_SIZE 60

enum {
    BLOCK_FIRST  = 1 << 0,
    BLOCK_MIDDLE = 1 << 1,
    BLOCK_LAST   = 1 << 2
};

struct block {
    uint8_t type;
    uint8_t reserved;
    uint16_t buf_size;
    char payload[PAYLOAD_SIZE];
} __attribute__((packed));

struct thread_safe_buf_queue buf_queue;

int draft_send(const void *buf, size_t size);
int draft_recv(void *buf, size_t size);


void* sender_thread(void* arg)
{
    (void)arg;

    char msg1[] = "Hello world";
    char msg2[150];
    char msg3[150];

    int s;

    for (int i = 0; i < sizeof(msg2); i++) {
        msg2[i] = 'A' + (i % 26);
        msg3[i] = 'a' + (i % 26);
    }

    s = draft_send(msg1, sizeof(msg1));
    printf("Sent %d bytes of msg1\n", s);

    s = draft_send(msg2, sizeof(msg2));
    printf("Sent %d bytes of msg2\n", s);

    s = draft_send(msg3, sizeof(msg3));
    printf("Sent %d bytes of msg3\n", s);

    return NULL;
}


void* receiver_thread(void* arg)
{
    (void)arg;

    char recv_buf1[100] = {0};
    char recv_buf2[100] = {0};
    char recv_buf3[200]  = {0};

    int r;

    /* TEST 1 */
    r = draft_recv(recv_buf1, sizeof(recv_buf1));
    printf("[RECV] TEST1 received %d bytes: \"%s\"\n", r, recv_buf1);

    /* TEST 2 */
    r = draft_recv(recv_buf2, sizeof(recv_buf2));
    printf("[RECV] TEST2 received %d bytes, first 50: \"%.50s\"\n", r, recv_buf2);

    /* TEST 3: recv < send */
    r = draft_recv(recv_buf3, sizeof(recv_buf3));
    printf("[RECV] TEST3 received %d bytes (buffer=30)\n", r);
    printf("[RECV] TEST3 data: \"%.30s\"\n", recv_buf3);

    return NULL;
}


/* ================= MAIN ================= */

int main(void)
{
    printf("sizeof(struct block) = %zu\n", sizeof(struct block));

    thread_safe_buf_queue_init(&buf_queue);

    pthread_t send_tid;
    pthread_t recv_tid;

    pthread_create(&send_tid, NULL, sender_thread, NULL);
    pthread_create(&recv_tid, NULL, receiver_thread, NULL);

    pthread_join(send_tid, NULL);
    pthread_join(recv_tid, NULL);

    thread_safe_buf_queue_destroy(&buf_queue);
    return 0;
}

int draft_send(const void *buf, size_t size)
{
	if (size == 0 || size > 65535)
		return -1;

	uint16_t sent_bytes = 0;

	while (sent_bytes < size) {
		struct block block = {0};

		uint16_t remaining_size = (uint16_t)size - sent_bytes;
		uint16_t copy_size = (remaining_size > PAYLOAD_SIZE) ? PAYLOAD_SIZE : remaining_size;

		if (sent_bytes == 0) {
			block.type = BLOCK_FIRST;
			block.buf_size = (uint16_t)size;

			if (remaining_size <= PAYLOAD_SIZE)
				block.type |= BLOCK_LAST;
		} else {
			block.type = (remaining_size <= PAYLOAD_SIZE) ? BLOCK_LAST : BLOCK_MIDDLE;
		}

		memcpy(block.payload, (char*)buf + sent_bytes, copy_size);
		sent_bytes += copy_size;

		thread_safe_buf_queue_push(&buf_queue, (char *)&block);
	}

	return (int)sent_bytes;
}

int draft_recv(void *buf, size_t size)
{
	if (size == 0 || size > 65535)
		return -1;

	uint16_t total_size = 0;
	uint16_t recv_size = 0;
    int is_session_active = 0;

	while (1) {
		char block_buf[BLOCK_SIZE];
        thread_safe_buf_queue_pop(&buf_queue, block_buf);
        struct block* block = (struct block*)block_buf;

		if (block->type & BLOCK_FIRST) {
			total_size = block->buf_size;
			recv_size = 0;
			is_session_active = 1;
		}

		if (!is_session_active)
			continue;

		if (recv_size < size) {
			uint16_t total_remaining_size = total_size - recv_size;
			uint16_t available_copy_size = (total_remaining_size > PAYLOAD_SIZE) ? PAYLOAD_SIZE : total_remaining_size;
			uint16_t remaining_size = (uint16_t)size - recv_size;
            size_t copy_size = (available_copy_size < remaining_size) 
                                   ? available_copy_size 
                                   : remaining_size;

            if (copy_size > 0) {
                memcpy((char*)buf + recv_size, block->payload, copy_size);
                recv_size += copy_size;
            }
		}

		if (block->type & BLOCK_LAST)
            return (int)recv_size;
	}
}
