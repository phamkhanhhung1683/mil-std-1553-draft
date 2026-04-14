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

int thread_safe_data_packet_queue_send_buf(struct thread_safe_data_packet_queue* q, const void *buf, size_t size, uint16_t msg_id)
{
    if (size == 0 || size > 65535)
		return -1;

	uint16_t byte_pos = 0;
    while (byte_pos < size) {
        struct data_packet fragment = {0};
        uint16_t remaining = (uint16_t)size - byte_pos;
        uint16_t current_payload = (remaining > DATA_MAX_LENGTH) ? DATA_MAX_LENGTH : remaining;

        fragment.id = msg_id;
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
    if (size == 0 || size > 65535)
		return -1;

	struct data_packet pkt;
    uint16_t current_msg_id = 0;
    uint16_t expected_offset_units = 0; // Đơn vị là 4 bytes
    bool is_assembling = false;
    uint8_t *output = (uint8_t *)buf;

    while (1) {
        // 1. Pop gói tin (Blocking)
        thread_safe_data_packet_queue_pop(q, &pkt);

        uint16_t pkt_offset_units = data_packet_get_offset(&pkt);
        uint8_t mf = data_packet_get_mf(&pkt);

        // CASE 4 & 1: Kiểm tra ID mới hoặc bắt đầu lại
        if (!is_assembling || pkt.id != current_msg_id) {
            // Trường hợp khởi đầu: Bắt buộc mảnh đầu tiên phải có Offset = 0
            if (pkt_offset_units == 0) {
                is_assembling = true;
                current_msg_id = pkt.id;
                expected_offset_units = 0;
                // Bắt đầu ghép bản tin mới
            } else {
                // Nếu ID mới mà offset != 0, hoặc queue bắt đầu bằng mảnh giữa -> Bỏ qua
                is_assembling = false;
                continue;
            }
        }

        // --- Kiểm tra tính liên tục của Offset ---
        
        // CASE 2: Trùng Offset (Duplicate) -> Bỏ qua mảnh này, đợi mảnh tiếp theo
        if (pkt_offset_units < expected_offset_units) {
            continue;
        }

        // CASE 3: Nhảy Offset (Missed Packet) -> Hỏng bản tin hiện tại, reset trạng thái
        if (pkt_offset_units > expected_offset_units) {
            is_assembling = false;
            // Nếu mảnh "nhảy cóc" này vô tình là offset 0 của ID khác, ta có thể xử lý lại
            // nhưng để đơn giản: bỏ qua và đợi chu kỳ nhận diện ID mới ở vòng lặp sau.
            continue;
        }

        // --- Xử lý dữ liệu khi Offset hợp lệ (pkt_offset == expected_offset) ---
        uint32_t offset_bytes = pkt_offset_units * 4;

        // Logic Truncation (Socket-like)
        if (offset_bytes < size) {
            size_t space_left = size - offset_bytes;
            size_t copy_len = (DATA_MAX_LENGTH > space_left) ? space_left : DATA_MAX_LENGTH;
            if (copy_len > 0) {
                memcpy(output + offset_bytes, pkt.data, copy_len);
            }
        }

        // Cập nhật offset kỳ vọng cho mảnh tiếp theo (mỗi mảnh 60 bytes = 15 đơn vị offset)
        expected_offset_units += (DATA_MAX_LENGTH / 4);

        // Kiểm tra kết thúc bản tin
        if (mf == 0) {
            is_assembling = false;
            uint32_t total_size = offset_bytes + DATA_MAX_LENGTH;
            return (total_size > size) ? (int)size : (int)total_size;
        }
    }
}
