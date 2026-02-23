# 1553 Remote Terminal (RT) Operation

If you read a CDP buffer while the firmware is in the middle of processing a message for that buffer, then the CDP Status Word will be 0xFFFFFFFF. If you see this value, then you should read the buffer again until the CDP Status Word is NOT 0xFFFFFFFF – you will then have a complete CDP buffer. If you use interrupts to synchronize buffer reads to messages on the bus then you should not see this case.

Most applications only use a single CDP buffer for a given SA. It is best to read and write buffers synchronously with messages on the bus – the usual approach is to enable an interrupt on each CDP, when the application gets the interrupt it reads or writes the appropriate CDP buffer (see example program ADT_L1_1553_ex_rt3int.c).

# 1553 Bus Controller (BC) Operation

## DELAYONLY

A DELAYONLY BCCB (BCCB CSR bit 22) causes the firmware’s protocol engine to halt execution of the BCCB list for the specified delay time, or until the specified time since StartFrame. A DELAYONLY BCCB is primarily used to provide entry points within a periodic BCCB list where aperiodic messages can be processed (refer to section ‘Aperiodic Messages’ for detailed information).

## Note on Inter-Message Gap Time & Frame Message Scheduling



# 1553 Device Interrupts

Alta ENET and USB devices do not use hardware interrupts. Polling must be used.

This draft uses the ADT_PRODUCT_NLINE_U1553 device.

## 1553 Software Polled Interrupts

For polling, the developer will need to decide when and how to call the ISR, usually through some sort of software timer. Remember that you should probably poll at a Nyquist rate (usually 2x the event rate that you are trying to capture).

## BC Interrupts

### BCCB Complete verses BCCB CDP Interrupt

The BCCB Complete interrupt is used to signify the BCCB transmission is complete, but without regard to which CDP for that BCCB was completed. If there is only one CDP for the BCCB, then setting either interrupt yields the same result – the user was notified that this one message was sent and there is only one CDP buffer. You should only need to use the BCCB Complete Interrupt if you want to be signaled that the respective BCCB message completed and you do not care about which CDP for the BCCB was executed.
