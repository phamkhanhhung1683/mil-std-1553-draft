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
#define DEVID (ADT_PRODUCT_NLINE_U1553 | ADT_DEVID_BOARDNUM_01 | ADT_DEVID_CHANNELTYPE_1553 | ADT_DEVID_CHANNELNUM_01)

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

int bc_init();
void bc_close();
int bc_send(const void *buf, size_t size);
int bc_recv(void *buf, size_t size);

static void *interrupt_poll(void *arg);

/* Prototype for our interrupt handler function */
static void ADT_L0_CALL_CONV myIntHandler(void * pUserData);

int main()
{
	int s;
	
	s = bc_init();
	if (s != 0) {
		return -1;
	}

	char msg1[] = "Hello World";
	s = bc_send(msg1, sizeof(msg1));

	char msg2[100];
	s = bc_recv(msg2, sizeof(msg2));

	bc_close();

	return 0;
}

int bc_init()
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

	/* BC Initialization - max 100 messages, 1 minor per major, BC CSR 0 */
	printf("Initializing BC . . . ");
	status = ADT_L1_1553_BC_Init(DEVID, 100, 1, 0);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Allocate BCCB and one CDP for message 0 */
	printf("Allocating BCCB 0 . . . ");
	status = ADT_L1_1553_BC_CB_CDPAllocate(DEVID, 0, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Allocate BCCB and one CDP for message 1 */
	printf("Allocating BCCB 1 . . . ");
	status = ADT_L1_1553_BC_CB_CDPAllocate(DEVID, 1, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Allocate BCCB and one CDP for message 2 */
	printf("Allocating BCCB 2 . . . ");
	status = ADT_L1_1553_BC_CB_CDPAllocate(DEVID, 2, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Define the BCCB for message 0  - DELAY ONLY TYPE (ACTIVE NOOP) - Start of Frame */
	/* Note that we are using a "DELAY ONLY" block as our "Start of Frame" marker rather
	   than just setting "Start of Frame" on the BCRT message.  We do this because we
	   will interrupt on the RTBC message at the end of frame, and the ISR will need 
	   time to change the BCRT data before the BCRT message is loaded into the encoder
	   for transmission.  If we set "Start of Frame" on the BCRT message, it gets
	   immediately loaded into the encoder at end of frame (before the ISR can update
	   the CDP with the new data).  By using a "DELAY ONLY" block as start of frame, 
	   the BCRT message is not loaded into the encoder until the frame timer has expired,
	   which gives us much more time for the ISR to update the BCRT data buffer. */
	ADT_L1_1553_BC_CB myBCCB;
	ADT_L1_1553_CDP myCdp;

	memset(&myBCCB, 0, sizeof(myBCCB));
	myBCCB.Csr = ADT_L1_1553_BC_CB_CSR_DELAYONLY |
					ADT_L1_1553_BC_CB_CSR_STARTFRAME; 	/* Start of frame */
	/* If STARTFRAME is set, then the FrameTime field specifies the frame time. */
	myBCCB.FrameTime = 20000;							/* 2 millisecond frame time (100ns LSB), 100 Hz frame */
	myBCCB.DelayTime = 0;								/* No Delay, this is just our Start of Frame marker */
	myBCCB.NextMsgNum = 1;								/* Set NEXT msg number to point to next message */

	printf("Writing BCCB for Start of Frame (Delay/NOOP) . . . ");
	status = ADT_L1_1553_BC_CB_Write(DEVID, 0, &myBCCB);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Define the BCCB for message 1 */
	memset(&myBCCB, 0, sizeof(myBCCB));
	myBCCB.CMD1Info = 0x0821;							/* BCRT 1-R-1-1 on Bus A */
	myBCCB.Csr = ADT_L1_1553_BC_CB_CSR_TYPE_BCRT |
				 ADT_L1_1553_BC_CB_CSR_BUSA;
	myBCCB.NextMsgNum = 2;								/* Set NEXT msg number to point to second message */

	printf("Writing BCCB for first message (BCRT 1-R-1-1) . . . ");
	status = ADT_L1_1553_BC_CB_Write(DEVID, 1, &myBCCB);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Write the data buffer (CDP) for message 1 */
	memset(&myCdp, 0, sizeof(myCdp));
	printf("Writing CDP buffer for first message . . . ");
	myCdp.DATAinfo[0] = 0;  /* This message only uses one data word */
	status = ADT_L1_1553_BC_CB_CDPWrite(DEVID, 1, 0, &myCdp);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Define the second BCCB for message 2 */
	memset(&myBCCB, 0, sizeof(myBCCB));
	myBCCB.CMD1Info = 0x0C21;							/* BCRT 1-T-1-1 on Bus A */
	myBCCB.Csr = ADT_L1_1553_BC_CB_CSR_INTMSGCOMP | 	/* Interrupt on Message Complete */
				 ADT_L1_1553_BC_CB_CSR_TYPE_RTBC |
				 ADT_L1_1553_BC_CB_CSR_BUSA |
				 ADT_L1_1553_BC_CB_CSR_ENDFRAME;		/* End of Frame */
	myBCCB.DelayTime = 1000;							/* 100us delay between messages being set */
	myBCCB.NextMsgNum = 0;								/* Set NEXT msg number to point to first message */

	printf("Writing BCCB for second message (RTBC 1-T-1-1) . . . ");
	status = ADT_L1_1553_BC_CB_Write(DEVID, 2, &myBCCB);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	printf("Starting BC . . . ");
	status = ADT_L1_1553_BC_Start(DEVID, 0);
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

void bc_close()
{
	ADT_L0_UINT32 status;

	atomic_store(&running, false);
	pthread_join(polling_thread_id, NULL);

	thread_safe_buf_queue_destroy(&send_queue);
	thread_safe_buf_queue_destroy(&recv_queue);

	/* Stop BC */
	printf("Stopping BC . . . ");
	status = ADT_L1_1553_BC_Stop(DEVID);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	/* Free BC memory */
	printf("Closing BC . . . ");
	status = ADT_L1_1553_BC_Close(DEVID);
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

int bc_send(const void *buf, size_t size)
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

int bc_recv(void *buf, size_t size)
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

	/* Read and process interrupt queue entries */
	numInts = 0;
	status = ADT_L1_1553_INT_IQ_ReadNewEntries(DEVID, MAX_IQ_ENTRIES, &numInts, intType, intInfo);
	if ((status == ADT_SUCCESS) && numInts)
	{
		/* Loop through all the int events read - normally we should see only one interrupt event here */
		for (i = 0; i < numInts; i++) 
		{
			/* We are only using the BCCB interrupt type */
			if ((intType[i] & 0xFFFF0000) == ADT_L1_1553_IQP_TYPESEQ_BCCB)
			{
				ADT_L0_UINT32 bccbNum = intInfo[i];

				if (bccbNum == 1) {
					if (thread_safe_buf_queue_empty(&send_queue)) {
						memset(&myIntCdp, 0, sizeof(myIntCdp));
						status = ADT_L1_1553_BC_CB_CDPWrite(DEVID, 1, 0, &myIntCdp);
					} else {
						char buf[BLOCK_SIZE];
						thread_safe_buf_queue_pop(&send_queue, buf);

						memset(&myIntCdp, 0, sizeof(myIntCdp));
						for (i = 0; i < 32; i++) {
							ADT_L0_UINT16 data = ((ADT_L0_UINT16)buf[i * 2]) | ((ADT_L0_UINT16)buf[i * 2 + 1] << 8);
							myIntCdp.DATAinfo[i] |= data; 
						}

						status = ADT_L1_1553_BC_CB_CDPWrite(DEVID, 1, 0, &myIntCdp);
					}

				} else if (bccbNum == 2) {
					status = ADT_L1_1553_BC_CB_CDPRead(DEVID, 2, 0, &myIntCdp);
					
					if ((myIntCdp.DATAinfo[0] & 0xFFFF) != 0x0000) {
						char buf[BLOCK_SIZE];

						for (i = 0; i < 32; i++) {
							ADT_L0_UINT16 data = (ADT_L0_UINT16)(myIntCdp.DATAinfo[i] & 0xFFFF);
							buf[i * 2] = (char)(data & 0xFF);
							buf[i * 2 + 1] = (char)((data >> 8) & 0xFF);
						}

						thread_safe_buf_queue_push(&recv_queue, buf);
					}
				}
			}
		} 
	}
}