#ifndef _THREAD_SAFE_DATA_PACKET_QUEUE_H
#define _THREAD_SAFE_DATA_PACKET_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include <pthread.h>

#define DATA_PACKET_SIZE 64
#define MAX_DATA_LENGTH  60

#define NULL_FRAGMENT_FLAG_BIT  15
#define MORE_FRAGMENTS_FLAG_BIT 14
#define FRAGMENT_OFFSET_MASK    0x3FFFU

#define QUEUE_CAPACITY 8192

struct data_packet {
	uint8_t id;
    uint8_t data_length;

	/* frag_info: 16 bits
	 * [15] NF (Null Fragment) Flag
	 * [14] MF (More Fragments) Flag
	 * [13:0] Fragment Offset
	 */
	uint16_t frag_info;

	uint8_t data[MAX_DATA_LENGTH];
};

struct thread_safe_data_packet_queue {
    struct data_packet packets[QUEUE_CAPACITY];

    int head;
    int tail;
    int size;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

void thread_safe_data_packet_queue_init(struct thread_safe_data_packet_queue *q);
void thread_safe_data_packet_queue_destroy(struct thread_safe_data_packet_queue *q);

void thread_safe_data_packet_queue_push(struct thread_safe_data_packet_queue *q, const struct data_packet *pkt);
int thread_safe_data_packet_queue_try_push(struct thread_safe_data_packet_queue *q, const struct data_packet *pkt);

void thread_safe_data_packet_queue_pop(struct thread_safe_data_packet_queue *q, struct data_packet *pkt);
int thread_safe_data_packet_queue_try_pop(struct thread_safe_data_packet_queue *q, struct data_packet *pkt);

int thread_safe_data_packet_queue_push_buf(struct thread_safe_data_packet_queue *q, const void *buf, size_t size, uint8_t msg_id);
int thread_safe_data_packet_queue_pop_buf(struct thread_safe_data_packet_queue *q, void *buf, size_t size);

static inline uint8_t data_packet_get_null_fragment_flag(const struct data_packet *pkt)
{
    return (pkt->frag_info >> NULL_FRAGMENT_FLAG_BIT) & 0x1U;
}

static inline uint8_t data_packet_get_more_fragments_flag(const struct data_packet *pkt)
{
    return (pkt->frag_info >> MORE_FRAGMENTS_FLAG_BIT) & 0x1U;
}

static inline uint16_t data_packet_get_fragment_offset(const struct data_packet *pkt)
{
    return pkt->frag_info & FRAGMENT_OFFSET_MASK;
}

static inline void data_packet_set_null_fragment_flag(struct data_packet *pkt, uint8_t val)
{
    if (val)
        pkt->frag_info |= (1U << NULL_FRAGMENT_FLAG_BIT);
    else
        pkt->frag_info &= ~(1U << NULL_FRAGMENT_FLAG_BIT);
}

static inline void data_packet_set_more_fragments_flag(struct data_packet *pkt, uint8_t val)
{
    if (val)
        pkt->frag_info |= (1U << MORE_FRAGMENTS_FLAG_BIT);
    else
        pkt->frag_info &= ~(1U << MORE_FRAGMENTS_FLAG_BIT);
}

static inline void data_packet_set_fragment_offset(struct data_packet *pkt, uint16_t offset)
{
    pkt->frag_info &= ~FRAGMENT_OFFSET_MASK;
    pkt->frag_info |= (offset & FRAGMENT_OFFSET_MASK);
}

#endif  /* _THREAD_SAFE_DATA_PACKET_QUEUE_H */
