#ifndef _virtio_hw_h
#define _virtio_hw_h

#include "..\osdep.h"

void *hwCreateDevice(void *pHostDevice);
void hwDestroyDevice(void *pd);
unsigned short hwGetQueueSize(void *pHw, unsigned short queueIndex);
ULONG hwGetQueuePfn(void *pHw, unsigned short queueIndex);
void hwSetQueuePfn(void *pHw, unsigned short queueIndex, ULONG val);
BYTE hwReadInterruptStatus(void *pHw);
BYTE hwGetDeviceStatus(void *pHw);
void hwSetDeviceStatus(void *pHw, BYTE val);
void hwQueueNotify(void *pHw, WORD wValue);

void hwReceiveBuffer(void *pHw, void *buffer, size_t size);
void hwCompleteTx(void *pHw, int num);
void hwGetInterrups(void *pHw, unsigned long *ptx, unsigned long *prx);
void hwCheckInterrupt(void *pHw);

bool hwReadDeviceData(void *pHw, ULONG reg, BYTE *pval);
bool hwWriteDeviceData(void *pHw, ULONG reg, BYTE val);


#define RXQ_INTERRUPT_VECTOR    1
#define TXQ_INTERRUPT_VECTOR    2
#define CTL_INTERRUPT_VECTOR    4
#define AUX_INTERRUPT_VECTOR    8

#define RX_QUEUE_NUMBER         0
#define TX_QUEUE_NUMBER         1
#define CTL_QUEUE_NUMBER        2
#define AUX_QUEUE_NUMBER        22

#endif
