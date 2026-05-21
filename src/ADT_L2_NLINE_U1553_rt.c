#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>

#include "ADT_L1.h"
#include "ADT_L2_NLINE_U1553_bcrt.h"
#include "thread_safe_data_packet_queue.h"

#define DEVID (ADT_PRODUCT_NLINE_U1553 | ADT_DEVID_BOARDNUM_02 | ADT_DEVID_CHANNELTYPE_1553 | ADT_DEVID_CHANNELNUM_01)
#define MAX_IQ_ENTRIES 100

static struct thread_safe_data_packet_queue send_queue;
static struct thread_safe_data_packet_queue recv_queue;

static atomic_bool running = false;
static pthread_t polling_thread_id;

static void *interrupt_poll(void *arg);
static void myIntHandler();

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

	printf("Enabling RT 1 . . . ");
	status = ADT_L1_1553_RT_Enable(DEVID, 1);
	if (status != ADT_SUCCESS) {
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));
		return -1;
	}
	printf("Success.\n");

	printf("Starting RT operation . . . ");
	status = ADT_L1_1553_RT_Start(DEVID);
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

	thread_safe_data_packet_queue_destroy(&send_queue);
	thread_safe_data_packet_queue_destroy(&recv_queue);

	printf("Disabling RT 1 . . . ");
	status = ADT_L1_1553_RT_Disable(DEVID, 1);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	printf("Stopping RT operation . . . ");
	status = ADT_L1_1553_RT_Stop(DEVID);
	if (status == ADT_SUCCESS)
		printf("Success.\n");
	else
		printf("FAILURE - Error = %d %s\n", status, ADT_L1_Error_to_String(status));

	printf("Closing RT1 . . . ");
	status = ADT_L1_1553_RT_Close(DEVID, 1);
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

int l2_rt1_send(const void *buf, size_t size)
{
	static uint8_t msg_id = 0;
	int ret = thread_safe_data_packet_queue_push_buf(&send_queue, buf, size, msg_id);
	msg_id++;
	return ret;
}

int l2_rt1_recv(void *buf, size_t size)
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
	if ((status == ADT_SUCCESS) && numInts) {
		for (int i = 0; i < numInts; i++) {
			if ((intType[i] & 0xFFFF0000) == ADT_L1_1553_IQP_TYPESEQ_RTCDP) {
				ADT_L0_UINT32 tr = (intInfo[i] & 0x00040000) >> ADT_L1_1553_CDP_RAPI_RT_TR;
				ADT_L1_1553_CDP myIntCdp = {0};
				struct data_packet pkt = {0};

				if (tr == 0) {  /* 1st CDP of RT1 RECEIVE SA1 */
					status = ADT_L1_1553_RT_SA_CDPRead(DEVID, 1, 0, 1, 0, &myIntCdp);

					if ((status == ADT_SUCCESS)) {
						uint16_t *data_word = (uint16_t *)&pkt;
						for (int j = 0; j < 32; j++) {
							data_word[j] = myIntCdp.DATAinfo[j];
						}

						uint8_t nf = data_packet_get_null_fragment_flag(&pkt);
						if (nf == 0)
							thread_safe_data_packet_queue_try_push(&recv_queue, &pkt);
					}
				} else if (tr == 1) {  /* 1st CDP of RT1 TRANSMIT SA1 */
					int s = thread_safe_data_packet_queue_try_pop(&send_queue, &pkt);
					if (s == -1)
						data_packet_set_null_fragment_flag(&pkt, 1);

					uint16_t *data_word = (uint16_t *)&pkt;
					for (int j = 0; j < 32; j++) {
						myIntCdp.DATAinfo[j] = data_word[j];
					}

					ADT_L1_1553_RT_SA_CDPWrite(DEVID, 1, 1, 1, 0, &myIntCdp);
				}
			}
		}
	}
}
