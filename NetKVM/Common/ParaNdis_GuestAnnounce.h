#pragma once

#include "ParaNdis-Util.h"

class CGratARPPacketHolder : public CRefCountingObject, public CNdisAllocatable<CGratARPPacketHolder, 'NBLH'>
{
private:
    PNET_BUFFER_LIST m_NBL;
    NDIS_HANDLE m_handle;

public:
    CGratARPPacketHolder::CGratARPPacketHolder(PNET_BUFFER_LIST NBL, NDIS_HANDLE handle) : m_NBL(NBL), m_handle(handle){};
    ~CGratARPPacketHolder();

    PNET_BUFFER_LIST GetNBL() { return m_NBL; };

    CGratARPPacketHolder(const CGratARPPacketHolder&) = delete;
    CGratARPPacketHolder& operator= (const CGratARPPacketHolder&) = delete;

private:
    virtual void OnLastReferenceGone() override;
    DECLARE_CNDISLIST_ENTRY(CGratARPPacketHolder);
};

typedef struct _tagPARANDIS_ADAPTER PARANDIS_ADAPTER;

class CGratuitousArpPackets : public CPlacementAllocatable
{
private:
    CNdisList<CGratARPPacketHolder, CLockedAccess, CCountingObject> m_packets;
    PARANDIS_ADAPTER *m_Context;
public:
    CGratuitousArpPackets(PARANDIS_ADAPTER *pContext) : m_Context(pContext) {};
    ~CGratuitousArpPackets()
    {
        DestroyNBLs();
    }
    VOID DestroyNBLs()
    {
        m_packets.ForEachDetached([](CGratARPPacketHolder *GratARPPacket) { GratARPPacket->Release(); });
    }
    VOID CreateNBL(UINT32 IPV4);
    VOID SendNBLs();
    static CGratARPPacketHolder *GetCGratArpPacketFromNBL(PNET_BUFFER_LIST NBL);
};

bool CallCompletionForNBL(PARANDIS_ADAPTER * pContext, PNET_BUFFER_LIST NBL);