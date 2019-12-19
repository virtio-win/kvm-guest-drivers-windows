#pragma once

#include "ParaNdis-Util.h"

class CGuestAnnouncePacketHolder : public CRefCountingObject, public CNdisAllocatable<CGuestAnnouncePacketHolder, 'NBLH'>
{
private:
    PNET_BUFFER_LIST m_NBL;
    NDIS_HANDLE m_handle;
    bool m_isIPV4; /* Packet can be IPV4 or IPV6*/

public:
    CGuestAnnouncePacketHolder::CGuestAnnouncePacketHolder(PNET_BUFFER_LIST NBL, NDIS_HANDLE handle, bool isIPV4) :
        m_NBL(NBL), m_handle(handle), m_isIPV4(isIPV4){};
    ~CGuestAnnouncePacketHolder();

    PNET_BUFFER_LIST GetNBL() { return m_NBL; };

    CGuestAnnouncePacketHolder(const CGuestAnnouncePacketHolder&) = delete;
    CGuestAnnouncePacketHolder& operator= (const CGuestAnnouncePacketHolder&) = delete;
    bool isIPV4() { return m_isIPV4; } /* true if ipv4, false if ipv6 */

private:
    virtual void OnLastReferenceGone() override;
    DECLARE_CNDISLIST_ENTRY(CGuestAnnouncePacketHolder);
};

class CGuestAnnouncePackets : public CPlacementAllocatable
{
private:
    CNdisList<CGuestAnnouncePacketHolder, CLockedAccess, CCountingObject> m_packets;
    PARANDIS_ADAPTER *m_Context;
public:
    CGuestAnnouncePackets(PARANDIS_ADAPTER *pContext) : m_Context(pContext) {};
    ~CGuestAnnouncePackets()
    {
        m_packets.ForEachDetached([](CGuestAnnouncePacketHolder *GratARPPacket) { GratARPPacket->Release(); });
    }
    VOID DestroyIPV4NBLs()
    {
        m_packets.ForEachDetachedIf([](CGuestAnnouncePacketHolder *GratARPPacket) { return GratARPPacket->isIPV4(); },
            [](CGuestAnnouncePacketHolder *GratARPPacket) { GratARPPacket->Release(); });
    }
    VOID DestroyIPV6NBLs()
    {
        m_packets.ForEachDetachedIf([](CGuestAnnouncePacketHolder *GratARPPacket) { return !GratARPPacket->isIPV4(); },
            [](CGuestAnnouncePacketHolder *GratARPPacket) { GratARPPacket->Release(); });
    }
    VOID CreateNBL(UINT32 IPV4);
    VOID CreateNBL(USHORT *IPV6);
    VOID SendNBLs();
    static void NblCompletionCallback(PNET_BUFFER_LIST NBL);
    enum { cloneFlags = NDIS_CLONE_FLAGS_USE_ORIGINAL_MDLS };
private:
    VOID CreateNBL(PVOID packet, UINT size, bool isIPV4);
    EthernetArpFrame *CreateIPv4Packet(UINT32 IPV4);
    EthernetNSMFrame *CreateIPv6Packet(USHORT *IPV6);
};

bool CallCompletionForNBL(PARANDIS_ADAPTER * pContext, PNET_BUFFER_LIST NBL);