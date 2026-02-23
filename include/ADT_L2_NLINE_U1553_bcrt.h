#ifndef __ADT_L2_NLINE_U1553_BCRT_H
#define __ADT_L2_NLINE_U1553_BCRT_H

#include <stddef.h>

int bc_init();
void bc_close();
int bc_send(const void *buf, size_t size);
int bc_recv(void *buf, size_t size);

int rt1_init();
void rt1_close();
int rt1_send(const void *buf, size_t size);
int rt1_recv(void *buf, size_t size);

#endif  /* __ADT_L2_NLINE_U1553_BCRT_H */
