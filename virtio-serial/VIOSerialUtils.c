#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
	ULONG ulValue;

	ulValue = READ_PORT_ULONG((PULONG)(ulRegister));
	DPrintf(4, ("Read R[%x] = %x\n", (ULONG)(ulRegister), ulValue));
	return ulValue;
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
	DPrintf(4, ("Write R[%x] = %x\n", (ULONG)(ulRegister), ulValue));
	WRITE_PORT_ULONG( (PULONG)(ulRegister),(ULONG)(ulValue) );
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
	u8 bValue;

	bValue = READ_PORT_UCHAR((PUCHAR)(ulRegister));
	DPrintf(4, ("Read R[%x] = %x\n", (ULONG)(ulRegister), bValue));
	return bValue;
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
	DPrintf(4, ("Write R[%x] = %x\n", (ULONG)(ulRegister), bValue));
	WRITE_PORT_UCHAR((PUCHAR)(ulRegister),(UCHAR)(bValue));
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
	u16 wValue;

	wValue = READ_PORT_USHORT((PUSHORT)(ulRegister));
	DPrintf(4, ("Rd R[%x] = %x\n", (ULONG)(ulRegister), wValue));
	return wValue;
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
	DPrintf(4, ("Wr R[%x] = %x\n", (ULONG)(ulRegister), wValue));
	WRITE_PORT_USHORT((PUSHORT)(ulRegister),(USHORT)(wValue));
}
