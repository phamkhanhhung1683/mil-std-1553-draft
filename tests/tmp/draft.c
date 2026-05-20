#include <stdio.h>
#include <stdint.h>

// #include "thread_safe_buf_queue.h"
// #include "thread_safe_buf_queue.c"

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


int main() {
    printf("sizeof(struct block) = %zu\n", sizeof(struct block));

    thread_safe_buf_queue_init(&buf_queue);

    /* -------- TEST 1: gửi nhỏ hơn PAYLOAD_SIZE -------- */
    char send_buf1[] = "Hello thread_safe_buf_queue!";
    char recv_buf1[100] = {0};

    int sent = draft_send(send_buf1, sizeof(send_buf1));
    printf("TEST1: sent = %d bytes\n", sent);

    int received = draft_recv(recv_buf1, sizeof(recv_buf1));
    printf("TEST1: received = %d bytes\n", received);
    printf("TEST1: data = \"%s\"\n\n", recv_buf1);

    /* -------- TEST 2: gửi lớn hơn PAYLOAD_SIZE -------- */
    char send_buf2[150];
    for (int i = 0; i < sizeof(send_buf2); i++) {
        send_buf2[i] = 'A' + (i % 26);
    }

    char recv_buf2[150] = {0};

    sent = draft_send(send_buf2, sizeof(send_buf2));
    printf("TEST2: sent = %d bytes\n", sent);

    received = draft_recv(recv_buf2, sizeof(recv_buf2));
    printf("TEST2: received = %d bytes\n", received);

    printf("TEST2: first 50 bytes = \"%.50s\"\n\n", recv_buf2);

    /* -------- TEST 3: recv nhỏ hơn size gửi -------- */
    char recv_buf3[30] = {0};

    sent = draft_send(send_buf2, sizeof(send_buf2));
    printf("TEST3: sent = %d bytes\n", sent);

    received = draft_recv(recv_buf3, sizeof(recv_buf3));
    printf("TEST3: received = %d bytes\n", received);
    printf("TEST3: data = \"%.30s\"\n", recv_buf3);

    return 0;
}


int draft_send(const void *buf, size_t size) {
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

int draft_recv(void *buf, size_t size) {
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
