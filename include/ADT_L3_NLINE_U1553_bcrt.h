#ifndef _ADT_L3_NLINE_U1553_BCRT_H
#define _ADT_L3_NLINE_U1553_BCRT_H

#include <stddef.h>
#include <stdint.h>

struct header {
    uint8_t type;
    uint8_t seq_num;
    uint8_t ack_num;
};

struct packet {
    struct header header;
};

int bc_connect();
void bc_close();

int bc_send(const void *buf, size_t size);
int bc_recv(void *buf, size_t size);

int rt1_accept();
void rt1_close();

int rt1_send(const void *buf, size_t size);
int rt1_recv(void *buf, size_t size);

#endif  /* _ADT_L3_NLINE_U1553_BCRT_H */
