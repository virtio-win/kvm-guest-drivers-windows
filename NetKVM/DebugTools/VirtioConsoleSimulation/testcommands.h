#ifndef testcommands_h
#define testcommands_h

#include "osdep.h"

EXTERN_C BOOLEAN bUseMergedBuffers;
EXTERN_C BOOLEAN bUsePublishedIndices;
EXTERN_C BOOLEAN bHostHasVnetHdr;
EXTERN_C BOOLEAN bVirtioF_NotifyOnEmpty;
EXTERN_C BOOLEAN bAsyncTransmit;
EXTERN_C BOOLEAN bUseIndirectTx;
EXTERN_C BOOLEAN bMSIXUsed;


#define LogTestFlow(format, ...) DoPrint("[test]" format, __VA_ARGS__)

EXTERN_C void   FailCase(const char* format, ...);

EXTERN_C void   SimulationPrepare();

EXTERN_C void   SimulationFinish();

// schedules the TX buffer for transmit
// if can't schedule, fail
EXTERN_C void AddTxBuffers(ULONG startSerial, ULONG num);

// retrieves ready RX buffer, returns its length
// if buffer not available, fails
EXTERN_C void GetRxBuffer(PULONG pLenght);

// returns RX buffer with specified serial to the pool of buffers
EXTERN_C void ReturnRxBuffer(ULONG serial);

EXTERN_C void KickTx(void);

EXTERN_C void KickTxAlways(void);

EXTERN_C BOOLEAN TxRestart(void);

EXTERN_C BOOLEAN RxRestart(void);

EXTERN_C void TxEnableInterrupt();
EXTERN_C void TxDisableInterrupt();

EXTERN_C void RxEnableInterrupt();
EXTERN_C void RxDisableInterrupt();

EXTERN_C void RxReceivePacket(UCHAR fill);

// num = -1 to camplete all
// otherwise completes up to num consecutive async TX
EXTERN_C void CompleteTx(int num);

EXTERN_C void GetTxBuffer(ULONG serial);

EXTERN_C UCHAR GetDeviceData(UCHAR offset);

EXTERN_C void SetDeviceData(UCHAR offset, UCHAR val);

EXTERN_C void SetRxMode(UCHAR mode, BOOLEAN bOnOff);

EXTERN_C void VlansAdd(USHORT *tags, int num);

EXTERN_C void VlansDel(USHORT *tags, int num);

EXTERN_C void SetMacAddresses(int num);

/* management of packets until they're returned */
EXTERN_C void KeepRxPacket(void *buffer, ULONG serial);
EXTERN_C void *GetRxPacket(ULONG serial);
BOOLEAN KeepTxPackets();


typedef enum _tScriptEvent
{
    escriptEvtPreprocessOK,
    escriptEvtPreprocessFail,
    escriptEvtPreprocessStep,
    escriptEvtProcessStep,
    escriptEvtProcessOK,
    escriptEvtProcessFail,
}tScriptEvent;

typedef void (*tScriptCallback)(PVOID ref, tScriptEvent evt, const char *format, ...);

BOOLEAN RunScript(const char *name, PVOID ref, tScriptCallback callback);

#endif
