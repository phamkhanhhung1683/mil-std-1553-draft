#include "thread_safe_data_packet_queue.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pthread.h>

void thread_safe_data_packet_queue_init(struct thread_safe_data_packet_queue* q)
{
    q->head = 0;
    q->tail = 0;
    q->size = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void thread_safe_data_packet_queue_destroy(struct thread_safe_data_packet_queue* q)
{
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

void thread_safe_data_packet_queue_push(struct thread_safe_data_packet_queue* q, const struct data_packet* pkt)
{
    pthread_mutex_lock(&q->mutex);

    while (q->size == QUEUE_CAPACITY) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    q->packets[q->tail] = *pkt;

    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->size++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

int thread_safe_data_packet_queue_try_push(struct thread_safe_data_packet_queue* q, const struct data_packet* pkt)
{
    int ret = -1;

    pthread_mutex_lock(&q->mutex);

    if (q->size < QUEUE_CAPACITY) {
        q->packets[q->tail] = *pkt;

        q->tail = (q->tail + 1) % QUEUE_CAPACITY;
        q->size++;

        pthread_cond_signal(&q->not_empty);
        ret = 0;
    }

    pthread_mutex_unlock(&q->mutex);
    return ret;
}

void thread_safe_data_packet_queue_pop(struct thread_safe_data_packet_queue* q, struct data_packet* pkt)
{
    pthread_mutex_lock(&q->mutex);

    while (q->size == 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    *pkt = q->packets[q->head];

    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->size--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

int thread_safe_data_packet_queue_try_pop(struct thread_safe_data_packet_queue* q, struct data_packet* pkt)
{
    int ret = -1;

    pthread_mutex_lock(&q->mutex);

    if (q->size > 0) {
        *pkt = q->packets[q->head];

        q->head = (q->head + 1) % QUEUE_CAPACITY;
        q->size--;
        
        pthread_cond_signal(&q->not_full);
        ret = 0;
    }

    pthread_mutex_unlock(&q->mutex);
    return ret;
}

int thread_safe_data_packet_queue_send_buf(struct thread_safe_data_packet_queue* q, const void *buf, size_t size, uint8_t msg_id)
{
    if (size == 0 || size > 65532)
		return -1;

	uint16_t byte_pos = 0;
    while (byte_pos < size) {
        struct data_packet fragment = {0};

        uint16_t remaining = (uint16_t)size - byte_pos;
        uint16_t current_payload = (remaining > DATA_MAX_LENGTH) ? DATA_MAX_LENGTH : remaining;

        fragment.id = msg_id;
        fragment.data_length = (uint8_t)current_payload;
		data_packet_set_nf(&fragment, 0);
		data_packet_set_mf(&fragment, remaining > DATA_MAX_LENGTH);
		data_packet_set_offset(&fragment, byte_pos / 4);

        memcpy(fragment.data, (char*)buf + byte_pos, current_payload);

        thread_safe_data_packet_queue_push(q, &fragment);
        byte_pos += current_payload;
    }

    return (int)byte_pos;
}

int thread_safe_data_packet_queue_recv_buf(struct thread_safe_data_packet_queue* q, void *buf, size_t size)
{
    if (size == 0 || size > 65532)
        return -1;

    struct data_packet pkt;
    uint8_t current_msg_id = 0;
    uint16_t expected_offset_units = 0; 
    bool is_assembling = false;
    uint8_t *output = (uint8_t *)buf;

    while (1) {
        // Pop blocking
        thread_safe_data_packet_queue_pop(q, &pkt);

        uint16_t pkt_offset_units = data_packet_get_offset(&pkt);
        uint8_t mf = data_packet_get_mf(&pkt);

        // --- Kiểm tra điều kiện bắt đầu bản tin mới ---
        if (!is_assembling || pkt.id != current_msg_id) {
            if (pkt_offset_units == 0) {
                is_assembling = true;
                current_msg_id = pkt.id;
                expected_offset_units = 0;
            } else {
                // Bỏ qua các mảnh "mồ côi" không phải offset 0
                is_assembling = false;
                continue;
            }
        }

        // --- Kiểm tra tính toàn vẹn của Offset ---
        
        // Mảnh bị trùng (Duplicate) -> Bỏ qua
        if (pkt_offset_units < expected_offset_units) {
            continue;
        }

        // Mảnh bị nhảy cóc (Missed packet) -> Hủy bản tin hiện tại
        if (pkt_offset_units > expected_offset_units) {
            is_assembling = false;
            continue;
        }

        // --- Chép dữ liệu với cơ chế Truncation ---
        uint32_t offset_bytes = pkt_offset_units * 4;

        if (offset_bytes < size) {
            size_t space_left = size - offset_bytes;
            // Sử dụng pkt.data_length thay vì DATA_MAX_LENGTH để lấy đúng số byte gửi
            size_t copy_len = (pkt.data_length > space_left) ? space_left : pkt.data_length;
            
            if (copy_len > 0) {
                memcpy(output + offset_bytes, pkt.data, copy_len);
            }
        }

        // Cập nhật offset kỳ vọng: luôn cộng 15 (60/4) vì các mảnh giữa luôn phải full
        expected_offset_units += (DATA_MAX_LENGTH / 4);

        // --- Kết thúc bản tin ---
        if (mf == 0) {
            is_assembling = false;
            // Kích thước thực tế = vị trí bắt đầu mảnh cuối + độ dài mảnh cuối
            uint32_t total_actual_size = offset_bytes + pkt.data_length;
            
            return (total_actual_size > size) ? (int)size : (int)total_actual_size;
        }
    }
}
