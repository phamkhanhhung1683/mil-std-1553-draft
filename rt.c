#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

#include <pthread.h>

#include "ADT_L1.h"
#include "thread_safe_buf_queue.h"

/* The DEVICE ID is a 32-bit value that identifies the following:
 *		bits 28-31 = Backplane Type (0 = Simulated, 1 = PCI)
 *		bits 20-27 = Board Type (0 = SIM-1553, 1 = TEST-1553, 2 = PMC-1553, 3 = PC104P-1553, 4 = PCI-1553)
 *		bits 16-19 = Board Number (0 to 15)
 *      bits 8-15 = Channel Type (0x10 = 1553)
 *		bits 0-7 = Channel Number (0 to 255)
 *
 * A device ID of 0x10201000 specifies the first 1553 channel of the
 * first ADT PMC-1553 board found.
 * A device ID of 0x10201001 specifies the second 1553 channel of the
 * first ADT PMC-1553 board found.
 */
#define DEVID (ADT_PRODUCT_NLINE_U1553 | ADT_DEVID_BOARDNUM_02 | ADT_DEVID_CHANNELTYPE_1553 | ADT_DEVID_CHANNELNUM_01)

#define MAX_IQ_ENTRIES 100
#define BLOCK_SIZE     64
#define PAYLOAD_SIZE   60

enum {
    BLOCK_FIRST  = 1 << 0,
    BLOCK_MIDDLE = 1 << 1,
    BLOCK_LAST   = 1 << 2
};

struct block {
    uint8_t  type;
    uint8_t  reserved;
    uint16_t buf_size;
    char     payload[PAYLOAD_SIZE];
} __attribute__((packed));

static struct thread_safe_buf_queue send_queue;
static struct thread_safe_buf_queue recv_queue;

static atomic_bool running = false;
static pthread_t polling_thread_id;

int rt1_init();
void rt1_close();
int rt1_send(const void *buf, size_t size);
int rt1_recv(void *buf, size_t size);

static void *interrupt_poll(void *arg);

/* Prototype for our interrupt handler function */
static void ADT_L0_CALL_CONV myIntHandler(void *pUserData);

int main()
{
	int s = rt1_init();
	if (s != 0) {
		return -1;
	}

	char msg1[] = "Hello World";
	s = rt1_send(msg1, sizeof(msg1));

	char msg2[100];
	s = rt1_recv(msg2, sizeof(msg2));

	rt1_close();

	return 0;
}

int rt1_init()
{
	ADT_L0_UINT32 status;

	printf("Initializing Device with Reset and No Mem Test. . . ");
	status = ADT_L1_1553_InitDefault_ExtendedOptions(DEVID, MAX_IQ_ENTRIES, ADT_L1_API_DEVICEINIT_FORCEINIT |
																 			ADT_L1_API_DEVICEINIT_NOMEMTEST |
																 			ADT_L1_API_DEVICEINIT_ROOTPERESET);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* RT Initialization */
	/* This sets up the default structures for RT 1 with all subaddresses 
		* (RX and TX) using the same CDP buffer.  Therefore all subaddresses
		* are "wrapped" and all messages are legal.
		*
		* Note that we could initialize more RT addresses if desired - we
		* are using MULTIPLE RT mode.
		*/
	printf("Initializing RT 1 . . . ");
	status = ADT_L1_1553_RT_Init(DEVID, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	printf("Allocating one CDP for RT1 RECEIVE SA1 . . . ");
	status = ADT_L1_1553_RT_SA_CDPAllocate(DEVID, 1, 0, 1, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	printf("Allocating one CDP for RT1 TRANSMIT SA1 . . . ");
	status = ADT_L1_1553_RT_SA_CDPAllocate(DEVID, 1, 1, 1, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	ADT_L1_1553_CDP myRtCdp;

	memset(&myRtCdp, 0, sizeof(myRtCdp));
	myRtCdp.CDPControlWord |= ADT_L1_1553_CDP_CONTROL_INTERR | ADT_L1_1553_CDP_CONTROL_INTNOERR;
	status = ADT_L1_1553_RT_SA_CDPWrite(DEVID, 1, 0, 1, 0, &myRtCdp);
	if (status != ADT_SUCCESS) {
		printf("FAILURE Writing RT SA CDP, Error = %d\n", status);
		return -1;
	}

	memset(&myRtCdp, 0, sizeof(myRtCdp));
	myRtCdp.CDPControlWord |= ADT_L1_1553_CDP_CONTROL_INTERR | ADT_L1_1553_CDP_CONTROL_INTNOERR;
	status = ADT_L1_1553_RT_SA_CDPWrite(DEVID, 1, 1, 1, 0, &myRtCdp);
	if (status != ADT_SUCCESS) {
		printf("FAILURE Writing RT SA CDP, Error = %d\n", status);
		return -1;
	}

	/* Turn on the RT */
	printf("Enabling RT 1 . . . ");
	status = ADT_L1_1553_RT_Enable(DEVID, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Start RT operation for the channel */
	printf("Starting RT operation . . . ");
	status = ADT_L1_1553_RT_Start(DEVID);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	thread_safe_buf_queue_init(&send_queue);
	thread_safe_buf_queue_init(&recv_queue);

	atomic_store(&running, true);
	int s = pthread_create(&polling_thread_id, NULL, &interrupt_poll, NULL);
	if (s != 0) {
		printf("pthread_create failed");;
		return -1;
	}

	return 0;
}

void rt1_close()
{
	ADT_L0_UINT32 status;

	atomic_store(&running, false);
	pthread_join(polling_thread_id, NULL);

	thread_safe_buf_queue_destroy(&send_queue);
	thread_safe_buf_queue_destroy(&recv_queue);

	/* Turn off the RT */
	printf("Disabling RT 1 . . . ");
	status = ADT_L1_1553_RT_Disable(DEVID, 1);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	/* Stop RT operation for the channel */
	printf("Stopping RT operation . . . ");
	status = ADT_L1_1553_RT_Stop(DEVID);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	/* Free the memory for the RT */
	printf("Closing RT1 . . . ");
	status = ADT_L1_1553_RT_Close(DEVID, 1);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	/* Close and exit */
	printf("\nClosing . . . ");
	status = ADT_L1_CloseDevice(DEVID);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
}

int rt1_send(const void *buf, size_t size)
{
	if (size == 0 || size > 65535)
		return -1;

	uint16_t sent_bytes = 0;

	while (sent_bytes < size) {
		struct block block = {0};

		uint16_t remaining_size = (uint16_t)size - sent_bytes;
		uint16_t copy_size = (remaining_size > PAYLOAD_SIZE) ? PAYLOAD_SIZE : remaining_size;

		if (sent_bytes == 0) {
			block.type = BLOCK_FIRST;
			block.buf_size = (uint16_t)size;

			if (remaining_size <= PAYLOAD_SIZE)
				block.type |= BLOCK_LAST;
		} else {
			block.type = (remaining_size <= PAYLOAD_SIZE) ? BLOCK_LAST : BLOCK_MIDDLE;
		}

		memcpy(block.payload, (char*)buf + sent_bytes, copy_size);
		sent_bytes += copy_size;

		thread_safe_buf_queue_push(&send_queue, (char *)&block);
	}

	return (int)sent_bytes;
}

int rt1_recv(void *buf, size_t size)
{
	if (size == 0 || size > 65535)
		return -1;

	uint16_t total_size = 0;
	uint16_t recv_size = 0;
    int is_session_active = 0;

	while (1) {
		char block_buf[BLOCK_SIZE];
        thread_safe_buf_queue_pop(&recv_queue, block_buf);
        struct block* block = (struct block*)block_buf;

		if (block->type & BLOCK_FIRST) {
			total_size = block->buf_size;
			recv_size = 0;
			is_session_active = 1;
		}

		if (!is_session_active)
			continue;

		if (recv_size < size) {
			uint16_t total_remaining_size = total_size - recv_size;
			uint16_t available_copy_size = (total_remaining_size > PAYLOAD_SIZE) ? PAYLOAD_SIZE : total_remaining_size;
			uint16_t remaining_size = (uint16_t)size - recv_size;
            size_t copy_size = (available_copy_size < remaining_size) 
                                   ? available_copy_size 
                                   : remaining_size;

            if (copy_size > 0) {
                memcpy((char*)buf + recv_size, block->payload, copy_size);
                recv_size += copy_size;
            }
		}

		if (block->type & BLOCK_LAST)
            return (int)recv_size;
	}
}

static void *interrupt_poll(void *arg)
{
	while (atomic_load(&running)) {
		myIntHandler(NULL);  /* THIS IS WHERE WE POLL FOR INTERRUPTS */
		ADT_L1_msSleep(1);
	}
	return NULL;
}

static void ADT_L0_CALL_CONV myIntHandler(void *pUserData)
{
	ADT_L0_UINT32 status = ADT_SUCCESS;
	ADT_L0_UINT32 intType[MAX_IQ_ENTRIES], intInfo[MAX_IQ_ENTRIES], numInts, i;
	ADT_L1_1553_CDP myIntCdp;
	ADT_L0_UINT32 tr;

	/* Read and process interrupt queue entries */
	numInts = 0;
	status = ADT_L1_1553_INT_IQ_ReadNewEntries(DEVID, MAX_IQ_ENTRIES, &numInts, intType, intInfo);
	if ((status == ADT_SUCCESS) && numInts) {
		while (status == ADT_SUCCESS) {
			/* Loop through all the int events read */
			for (i = 0; i < numInts; i++) {
				if ((intType[i] & 0xFFFF0000) == ADT_L1_1553_IQP_TYPESEQ_RTCDP) {
					tr = (intInfo[i] & 0x00040000) >> ADT_L1_1553_CDP_RAPI_RT_TR;

					if (tr == 0) {  /* 1st CDP of RT1 RECEIVE SA1 */
						status = ADT_L1_1553_RT_SA_CDPRead(DEVID, 1, 0, 1, 0, &myIntCdp);

						if ((myIntCdp.DATAinfo[0] & 0xFFFF) != 0x0000) {
							char buf[BLOCK_SIZE];

							for (i = 0; i < 32; i++) {
								ADT_L0_UINT16 data = (ADT_L0_UINT16)(myIntCdp.DATAinfo[i] & 0xFFFF);
								buf[i * 2] = (char)(data & 0xFF);
								buf[i * 2 + 1] = (char)((data >> 8) & 0xFF);
							}

							thread_safe_buf_queue_push(&recv_queue, buf);
						}
					} else if (tr == 1) {  /* 1st CDP of RT1 TRANSMIT SA1 */
						if (thread_safe_buf_queue_empty(&send_queue)) {
							memset(&myIntCdp, 0, sizeof(myIntCdp));
							myIntCdp.CDPControlWord |= ADT_L1_1553_CDP_CONTROL_INTERR | ADT_L1_1553_CDP_CONTROL_INTNOERR;
							status = ADT_L1_1553_RT_SA_CDPWrite(DEVID, 1, 1, 1, 0, &myIntCdp);
						} else {
							char buf[BLOCK_SIZE];
							thread_safe_buf_queue_pop(&send_queue, buf);

							memset(&myIntCdp, 0, sizeof(myIntCdp));
							myIntCdp.CDPControlWord &= ~(ADT_L1_1553_CDP_CONTROL_INTERR | ADT_L1_1553_CDP_CONTROL_INTNOERR);
							for (i = 0; i < 32; i++) {
								ADT_L0_UINT16 data = ((ADT_L0_UINT16)buf[i * 2]) | ((ADT_L0_UINT16)buf[i * 2 + 1] << 8);
								myIntCdp.DATAinfo[i] |= data; 
							}

							status = ADT_L1_1553_RT_SA_CDPWrite(DEVID, 1, 1, 1, 0, &myIntCdp);
						}
					}
				}
			}
		}
	}
}