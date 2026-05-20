#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>

#include "ADT_L1.h"
#include "ADT_L2_NLINE_U1553_bcrt.h"
#include "thread_safe_data_packet_queue.h"

#define DEVID (ADT_PRODUCT_NLINE_U1553 | ADT_DEVID_BOARDNUM_01 | ADT_DEVID_CHANNELTYPE_1553 | ADT_DEVID_CHANNELNUM_01)
#define MAX_IQ_ENTRIES 100

static struct thread_safe_data_packet_queue send_queue;
static struct thread_safe_data_packet_queue recv_queue;

static atomic_bool running = false;
static pthread_t polling_thread_id;

static void *interrupt_poll(void *arg);
static void myIntHandler();

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
				 ADT_L1_1553_BC_CB_CSR_STARTFRAME; 		/* Start of frame */
	/* If STARTFRAME is set, then the FrameTime field specifies the frame time. */
	myBCCB.FrameTime = 40000;							/* 4 millisecond frame time (100ns LSB) */
	myBCCB.DelayTime = 0;								/* No Delay, this is just our Start of Frame marker */
	myBCCB.NextMsgNum = 1;								/* Go to message 1 */

	printf("Writing BCCB for Start of Frame (Delay/NOOP) . . . ");
	status = ADT_L1_1553_BC_CB_Write(DEVID, 0, &myBCCB);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Define the BCCB for message 1 */
	memset(&myBCCB, 0, sizeof(myBCCB));
	myBCCB.CMD1Info = cmdWord(1, 0, 1, 0);;				/* BCRT 1-R-1-32 on Bus A */
	myBCCB.Csr = ADT_L1_1553_BC_CB_CSR_INTMSGCOMP | 	/* Interrupt on Message Complete */
				 ADT_L1_1553_BC_CB_CSR_TYPE_BCRT |
				 ADT_L1_1553_BC_CB_CSR_BUSA;
	myBCCB.DelayTime = 1000;							/* 100 uSec IMG */
	myBCCB.NextMsgNum = 2;								/* Go to message 2 */

	printf("Writing BCCB for message 1 (BCRT 1-R-1-32) . . . ");
	status = ADT_L1_1553_BC_CB_Write(DEVID, 1, &myBCCB);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Write the data buffer (CDP) for message 1 */
	memset(&myCdp, 0, sizeof(myCdp));
	printf("Writing CDP buffer for first message . . . ");
	status = ADT_L1_1553_BC_CB_CDPWrite(DEVID, 1, 0, &myCdp);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	/* Define the second BCCB for message 2 */
	memset(&myBCCB, 0, sizeof(myBCCB));
	myBCCB.CMD1Info = cmdWord(1, 1, 1, 0);				/* RTBC 1-T-1-32 on Bus A */
	myBCCB.Csr = ADT_L1_1553_BC_CB_CSR_INTMSGCOMP | 	/* Interrupt on Message Complete */
				 ADT_L1_1553_BC_CB_CSR_TYPE_RTBC |
				 ADT_L1_1553_BC_CB_CSR_BUSA |
				 ADT_L1_1553_BC_CB_CSR_ENDFRAME;		/* End of Frame */
	myBCCB.DelayTime = 1000;							/* 100 uSec IMG */
	myBCCB.NextMsgNum = 0;								/* Go to message 0 */

	printf("Writing BCCB for message 2 (RTBC 1-T-1-32) . . . ");
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

	thread_safe_data_packet_queue_init(&send_queue);
	thread_safe_data_packet_queue_init(&recv_queue);

	atomic_store(&running, true);

	int s = pthread_create(&polling_thread_id, NULL, &interrupt_poll, NULL);
	if (s != 0) {
		printf("pthread_create failed\n");
		return -1;
	}

	return 0;
}

void bc_close()
{
	ADT_L0_UINT32 status;

	atomic_store(&running, false);
	pthread_join(polling_thread_id, NULL);

	thread_safe_data_packet_queue_destroy(&send_queue);
	thread_safe_data_packet_queue_destroy(&recv_queue);

	printf("Stopping BC . . . ");
	status = ADT_L1_1553_BC_Stop(DEVID);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	printf("Closing BC . . . ");
	status = ADT_L1_1553_BC_Close(DEVID);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	printf("\nClosing . . . ");
	status = ADT_L1_CloseDevice(DEVID);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
}

int bc_send(const void *buf, size_t size)
{
	static uint8_t msg_id = 0;
	int ret = thread_safe_data_packet_queue_push_buf(&send_queue, buf, size, msg_id);
	msg_id++;
	return ret;
}

int bc_recv(void *buf, size_t size)
{
	return thread_safe_data_packet_queue_pop_buf(&recv_queue, buf, size);
}

static void *interrupt_poll(void *arg)
{
	while (atomic_load(&running)) {
		myIntHandler();
		ADT_L1_msSleep(1);
	}
	return NULL;
}

static void myIntHandler()
{
	ADT_L0_UINT32 intType[MAX_IQ_ENTRIES], intInfo[MAX_IQ_ENTRIES];
	ADT_L0_UINT32 numInts = 0;
	ADT_L0_UINT32 status = ADT_L1_1553_INT_IQ_ReadNewEntries(DEVID, MAX_IQ_ENTRIES, &numInts, intType, intInfo);
	if ((status != ADT_SUCCESS) || (numInts == 0))
		return;
	
	for (int i = 0; i < numInts; i++) {
		if ((intType[i] & 0xFFFF0000) == ADT_L1_1553_IQP_TYPESEQ_BCCB)
		{
			ADT_L0_UINT32 bccbNum = intInfo[i];
			ADT_L1_1553_CDP myIntCdp = {0};
			struct data_packet pkt = {0};

			if (bccbNum == 1) {  // BC sent data packet to RT
				int s = thread_safe_data_packet_queue_try_pop(&send_queue, &pkt);
				if (s == -1)
					data_packet_set_null_fragment_flag(&pkt, 1);

				uint16_t *data_word = (uint16_t *)&pkt;
				for (int j = 0; j < 32; j++) {
					myIntCdp.DATAinfo[j] = data_word[j];
				}

				ADT_L1_1553_BC_CB_CDPWrite(DEVID, 1, 0, &myIntCdp);
			} else if (bccbNum == 2) {  // BC received data packet from RT
				status = ADT_L1_1553_BC_CB_CDPRead(DEVID, 2, 0, &myIntCdp);
				if ((status == ADT_SUCCESS)) {
					uint16_t *data_word = (uint16_t *)&pkt;
					for (int j = 0; j < 32; j++) {
						data_word[j] = myIntCdp.DATAinfo[j];
					}

					uint8_t nf = data_packet_get_null_fragment_flag(&pkt);
					if (nf == 0)
						thread_safe_data_packet_queue_try_push(&recv_queue, &pkt);
				}
			}
		}
	}
}
