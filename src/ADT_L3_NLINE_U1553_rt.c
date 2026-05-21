#include "ADT_L2_NLINE_U1553_bcrt.h"
#include "ADT_L3_NLINE_U1553_bcrt.h"

enum type {
    TYPE_SYNT = 0
};

int rt1_accept() {
    while (1) {
        struct header rx_pkt_header;
        int n;
        n = l2_rt1_recv(&rx_pkt_header, sizeof(rx_pkt_header));
        if (n == sizeof(rx_pkt_header) && rx_pkt_header.type == TYPE_SYNT) {
            struct header tx_pkt_header;
            tx_pkt_header.ack_num = 1U - rx_pkt_header.seq_num;
            tx_pkt_header.seq_num = 0U;

            l2_rt1_send(&tx_pkt_header, sizeof(tx_pkt_header));
            return 0;
        }
    }
}
