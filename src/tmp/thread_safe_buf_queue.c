#define _GNU_SOURCE
#include "thread_safe_buf_queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pthread.h>
#include <sys/mman.h>

#define QUEUE_CAPACITY 524288

bool thread_safe_buf_queue_init(struct thread_safe_buf_queue *q)
{
    q->head = 0;
    q->tail = 0;
    pthread_mutex_init(&q->mutex, NULL);

    int fd = memfd_create("magic_buffer", 0);
    if (fd < 0)
        return false;

    if (ftruncate(fd, QUEUE_CAPACITY) < 0) {
        close(fd);
        return false;
    }
    uint8_t* address_space = mmap(NULL, 2 * QUEUE_CAPACITY, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (address_space == MAP_FAILED) {
        close(fd);
        return false;
    }

    // 3. Ánh xạ lần 1 vào nửa đầu vùng địa chỉ ảo
    uint8_t* buffer_v1 = mmap(address_space, QUEUE_CAPACITY, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (buffer_v1 == MAP_FAILED) {
        munmap(address_space, 2 * QUEUE_CAPACITY);
        close(fd);
        return false;
    }

    // 4. Ánh xạ lần 2 (chính file đó) vào nửa sau vùng địa chỉ ảo, nối liền ngay sau nửa đầu
    uint8_t* buffer_v2 = mmap(address_space + QUEUE_CAPACITY, QUEUE_CAPACITY, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (buffer_v2 == MAP_FAILED) {
        munmap(address_space, 2 * QUEUE_CAPACITY);
        close(fd);
        return false;
    }

    // File descriptor không còn cần thiết sau khi đã mmap thành công
    close(fd);

    q->storage = address_space;
    return true;
}

void thread_safe_buf_queue_destroy(struct thread_safe_buf_queue *q)
{
    munmap(q->storage, 2 * QUEUE_CAPACITY);
    pthread_mutex_destroy(&q->mutex);
}

int thread_safe_buf_queue_push(struct thread_safe_buf_queue *q, const void *buf, size_t size)
{
    // Giới hạn độ dài gói tin tối đa bằng 2 bytes header (65535 bytes)
    if (size > 0xFFFF) {
        return -1;
    }

    uint16_t packet_len = (uint16_t)size; 
    uint32_t total_needed = 2 + packet_len;
    int bytes_written = -1; 

    pthread_mutex_lock(&q->mutex);

    // Tính toán dung lượng trống liên tục dựa trên khoảng cách head/tail
    uint32_t used_space = q->tail - q->head;
    uint32_t free_space = QUEUE_CAPACITY - used_space;

    if (free_space >= total_needed) {
        // Phép toán lấy vị trí tail thực tế dựa trên mặt nạ vòng của Magic Buffer
        uint32_t target_tail = q->tail % QUEUE_CAPACITY;

        // Lưu 2 bytes độ dài trước, sau đó ghi thẳng tuột dữ liệu
        memcpy(&q->storage[target_tail], &packet_len, 2);
        memcpy(&q->storage[target_tail + 2], buf, packet_len);

        q->tail += total_needed; // Tịnh tiến tail tuyến tính
        bytes_written = (int)size; // Trả về số byte dữ liệu thực tế (không tính 2 byte header)
    }

    pthread_mutex_unlock(&q->mutex);
    return bytes_written;
}

// ==================== HÀM POP (Giống recv()) ====================
// Trả về: số bytes đã lấy ra (>0), hoặc 0 nếu queue rỗng, hoặc -1 nếu buffer đầu ra quá nhỏ
int thread_safe_buf_queue_pop(struct thread_safe_buf_queue *q, void *buf, size_t size)
{
    int bytes_read = 0; // Mặc định trả về 0 nếu queue rỗng

    pthread_mutex_lock(&q->mutex);

    if (q->tail != q->head) { // Kiểm tra queue không rỗng
        uint32_t target_head = q->head % QUEUE_CAPACITY;
        uint16_t length;
        
        // Đọc 2 bytes header để lấy chiều dài gói tin thực tế
        memcpy(&length, &q->storage[target_head], 2);

        // Kiểm tra xem bộ đệm người dùng cấp cho hàm Pop có đủ lớn để chứa gói tin không
        if (length <= size) {
            // Đọc dữ liệu ra ngoài thẳng tuột nhờ cơ chế nhân đôi vùng nhớ của Linux
            memcpy(buf, &q->storage[target_head + 2], length);
            
            q->head += (2 + length); // Tịnh tiến head tuyến tính
            bytes_read = (int)length; // Trả về số byte đọc được
        } else {
            bytes_read = -1;
        }
    }

    pthread_mutex_unlock(&q->mutex);
    return bytes_read;
}
