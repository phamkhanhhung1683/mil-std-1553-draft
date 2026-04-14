#ifndef _THREAD_SAFE_DATA_PACKET_QUEUE_H
#define _THREAD_SAFE_DATA_PACKET_QUEUE_H

#include <stdint.h>

#include <pthread.h>

#define DATA_PACKET_SIZE 64
#define DATA_MAX_LENGTH  60

#define FRAG_NF_BIT         15
#define FRAG_MF_BIT         14
#define FRAG_OFFSET_MASK    0x3FFF

#define QUEUE_CAPACITY 12800

struct data_packet {
	uint8_t id;
    uint8_t data_length;

	/* frag_info: 16 bits
	 * [15] NF (Null Fragment)
	 * [14] MF (More Fragments)
	 * [13:0] Fragment Offset (14 bits)
	 */
	uint16_t frag_info;

	uint8_t data[DATA_MAX_LENGTH];
} __attribute__((packed));

struct thread_safe_data_packet_queue {
    struct data_packet packets[QUEUE_CAPACITY];

    int head;
    int tail;
    int size;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

void thread_safe_data_packet_queue_init(struct thread_safe_data_packet_queue* q);
void thread_safe_data_packet_queue_destroy(struct thread_safe_data_packet_queue* q);

void thread_safe_data_packet_queue_push(struct thread_safe_data_packet_queue* q, const struct data_packet* pkt);
int thread_safe_data_packet_queue_try_push(struct thread_safe_data_packet_queue* q, const struct data_packet* pkt);

void thread_safe_data_packet_queue_pop(struct thread_safe_data_packet_queue* q, struct data_packet* pkt);
int thread_safe_data_packet_queue_try_pop(struct thread_safe_data_packet_queue* q, struct data_packet* pkt);

int thread_safe_data_packet_queue_send_buf(struct thread_safe_data_packet_queue* q, const void *buf, size_t size, uint8_t msg_id);
int thread_safe_data_packet_queue_recv_buf(struct thread_safe_data_packet_queue* q, void *buf, size_t size);

static inline uint8_t data_packet_get_nf(const struct data_packet* pkt)
{
    return (pkt->frag_info >> FRAG_NF_BIT) & 0x1;
}

static inline uint8_t data_packet_get_mf(const struct data_packet* pkt)
{
    return (pkt->frag_info >> FRAG_MF_BIT) & 0x1;
}

static inline uint16_t data_packet_get_offset(const struct data_packet* pkt)
{
    return pkt->frag_info & FRAG_OFFSET_MASK;
}

static inline void data_packet_set_nf(struct data_packet* pkt, uint8_t val)
{
    if (val)
        pkt->frag_info |= (1 << FRAG_NF_BIT);
    else
        pkt->frag_info &= ~(1 << FRAG_NF_BIT);
}

static inline void data_packet_set_mf(struct data_packet* pkt, uint8_t val)
{
    if (val)
        pkt->frag_info |= (1 << FRAG_MF_BIT);
    else
        pkt->frag_info &= ~(1 << FRAG_MF_BIT);
}

static inline void data_packet_set_offset(struct data_packet* pkt, uint16_t offset)
{
    pkt->frag_info &= ~FRAG_OFFSET_MASK;
    pkt->frag_info |= (offset & FRAG_OFFSET_MASK);
}

#endif /* _THREAD_SAFE_DATA_PACKET_QUEUE_H */
