#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>

// Define the structure for a single node in the queue
typedef struct Node {
    unsigned char* buffer;
    size_t size;
    struct Node* next;
} Node;

// Define the structure for the thread-safe queue
typedef struct ThreadSafeQueue {
    Node* head;
    Node* tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty_cond; // Condition variable for consumers to wait on
    int count; // Track number of items
} ThreadSafeQueue;

// Function to initialize the queue
void init_queue(ThreadSafeQueue* q) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty_cond, NULL);
}

// Function to destroy the queue and free resources
void destroy_queue(ThreadSafeQueue* q) {
    // Lock before destroying to ensure no other operations are ongoing
    pthread_mutex_lock(&q->mutex);
    Node* current = q->head;
    while (current != NULL) {
        Node* temp = current;
        current = current->next;
        free(temp->buffer); // Free the byte buffer
        free(temp);         // Free the node
    }
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    pthread_mutex_unlock(&q->mutex);

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty_cond);
}

// Function to check if the queue is empty (must be called with mutex locked)
bool is_empty_internal(ThreadSafeQueue* q) {
    return (q->count == 0); // or (q->head == NULL)
}

// Thread-safe function to check if the queue is empty
bool is_empty(ThreadSafeQueue* q) {
    pthread_mutex_lock(&q->mutex);
    bool empty = is_empty_internal(q);
    pthread_mutex_unlock(&q->mutex);
    return empty;
}

// Function to enqueue a byte buffer
void enqueue(ThreadSafeQueue* q, unsigned char* buffer, size_t size) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    if (!new_node) {
        perror("Failed to allocate memory for node");
        exit(EXIT_FAILURE);
    }
    // Allocate memory for the buffer and copy the data
    new_node->buffer = (unsigned char*)malloc(size);
    if (!new_node->buffer) {
        perror("Failed to allocate memory for buffer");
        free(new_node);
        exit(EXIT_FAILURE);
    }
    memcpy(new_node->buffer, buffer, size);
    new_node->size = size;
    new_node->next = NULL;

    pthread_mutex_lock(&q->mutex);
    if (q->tail == NULL) {
        q->head = new_node;
        q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }
    q->count++;
    // Signal waiting consumers that the queue is no longer empty
    pthread_cond_signal(&q->not_empty_cond);
    pthread_mutex_unlock(&q->mutex);
}

// Function to dequeue a byte buffer (blocking if empty)
unsigned char* dequeue(ThreadSafeQueue* q, size_t* size_out) {
    pthread_mutex_lock(&q->mutex);

    // Wait while the queue is empty to prevent race conditions
    // The while loop protects against spurious wakeups
    while (is_empty_internal(q)) {
        pthread_cond_wait(&q->not_empty_cond, &q->mutex);
    }

    Node* temp = q->head;
    unsigned char* buffer = temp->buffer;
    *size_out = temp->size;

    q->head = temp->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    free(temp); // Free the node structure, but not the buffer it points to

    return buffer;
}

// Example usage
int main() {
    ThreadSafeQueue queue;
    init_queue(&queue);

    // ... Producer and Consumer threads would use enqueue/dequeue ...
    // e.g., enqueue(&queue, my_byte_array, array_size);
    // e.g., unsigned char* data = dequeue(&queue, &size);

    destroy_queue(&queue);
    return 0;
}
