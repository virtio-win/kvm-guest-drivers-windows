#pragma once

#include "ParaNdis-Util.h"

class CGratARPPacketHolder : public CRefCountingObject, public CNdisAllocatable<CGratARPPacketHolder, 'NBLH'>
{
private:
    PNET_BUFFER_LIST m_NBL;
    NDIS_HANDLE m_handle;
    bool m_isIPV4; /* Packet can be IPV4 or IPV6*/

public:
    CGratARPPacketHolder::CGratARPPacketHolder(PNET_BUFFER_LIST NBL, NDIS_HANDLE handle, bool isIPV4) :
        m_NBL(NBL), m_handle(handle), m_isIPV4(isIPV4){};
    ~CGratARPPacketHolder();

    PNET_BUFFER_LIST GetNBL() { return m_NBL; };

    CGratARPPacketHolder(const CGratARPPacketHolder&) = delete;
    CGratARPPacketHolder& operator= (const CGratARPPacketHolder&) = delete;
    bool isIPV4() { return m_isIPV4; } /* true if ipv4, false if ipv6 */

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
        m_packets.ForEachDetached([](CGratARPPacketHolder *GratARPPacket) { GratARPPacket->Release(); });
    }
    VOID DestroyIPV4NBLs()
    {
        m_packets.ForEachDetachedIf([](CGratARPPacketHolder *GratARPPacket) { return GratARPPacket->isIPV4(); },
            [](CGratARPPacketHolder *GratARPPacket) { GratARPPacket->Release(); });
    }
    VOID DestroyIPV6NBLs()
    {
        m_packets.ForEachDetachedIf([](CGratARPPacketHolder *GratARPPacket) { return !GratARPPacket->isIPV4(); },
            [](CGratARPPacketHolder *GratARPPacket) { GratARPPacket->Release(); });
    }
    VOID CreateNBL(UINT32 IPV4);
    VOID CreateNBL(USHORT *IPV6);
    VOID SendNBLs();
    static CGratARPPacketHolder *GetCGratArpPacketFromNBL(PNET_BUFFER_LIST NBL);
};

bool CallCompletionForNBL(PARANDIS_ADAPTER * pContext, PNET_BUFFER_LIST NBL);