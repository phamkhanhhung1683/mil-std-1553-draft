#include "ADT_L2_NLINE_U1553_bcrt.h"
#include "ADT_L3_NLINE_U1553_bcrt.h"

enum type {
    TYPE_SYNT = 0
};

int bc_connect() {
    while (1) {
        struct header tx_pkt_header;
        tx_pkt_header.type = TYPE_SYNT;
        tx_pkt_header.seq_num = 0;
        int n;
        n = l2_bc_send(&tx_pkt_header, sizeof(tx_pkt_header));
        if (n == sizeof(tx_pkt_header)) {
            struct header rx_pkt_header;
            n = l2_bc_recv(&rx_pkt_header, sizeof(rx_pkt_header));
            
        }
    }
}
