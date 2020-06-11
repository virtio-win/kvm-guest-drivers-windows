#include "ndis56common.h"
#include "ParaNdis_GuestAnnounce.h"
#include "ParaNdis6_Driver.h"
#include "ethernetutils.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_GuestAnnounce.tmh"
#endif

CGuestAnnouncePacketHolder::~CGuestAnnouncePacketHolder()
{
    PVOID buffer;
    PMDL mdl;
    if (m_NBL)
    {
        mdl = NET_BUFFER_CURRENT_MDL(NET_BUFFER_LIST_FIRST_NB(m_NBL));
        buffer = MmGetMdlVirtualAddress(mdl);
        if (mdl)
        {
            NdisFreeMdl(mdl);
        }
        if (buffer)
        {
            NdisFreeMemory(buffer, 0, 0);
        }
        NdisFreeNetBufferList(m_NBL);
    }
}

void CGuestAnnouncePacketHolder::OnLastReferenceGone()
{
    Destroy(this, m_handle);
}

EthernetArpFrame *CGuestAnnouncePackets::CreateIPv4Packet(UINT32 IPV4)
{
    EthernetArpFrame *packet = (EthernetArpFrame *)ParaNdis_AllocateMemory(m_Context, sizeof(EthernetArpFrame));
    if (!packet)
    {
        DPrintf(0, "Error could not allocate buffer for arp packet!\n");
        return NULL;
    }
    packet->frame.ether_type = _byteswap_ushort(ETH_ETHER_TYPE_ARP);
    packet->data.hardware_type = _byteswap_ushort(ETH_HARDWARE_TYPE);
    packet->data.protocol_type = _byteswap_ushort(ETH_IP_PROTOCOL_TYPE);
    packet->data.hardware_address_length = ETH_HARDWARE_ADDRESS_SIZE;
    packet->data.protocol_address_length = ETH_IPV4_ADDRESS_SIZE;
    packet->data.operation = _byteswap_ushort(ETH_ARP_OPERATION_TYPE_REQUEST);
    for (UINT i = 0; i < ETH_HARDWARE_ADDRESS_SIZE; i++)
    {
        packet->frame.sender_hardware_address.address[i] = (UINT8)m_Context->CurrentMacAddress[i];
        packet->frame.target_hardware_address.address[i] = 0xFF;
        packet->data.sender_hardware_address.address[i] = m_Context->CurrentMacAddress[i];
        // according to RFC 5227 ARP announcement shall set target hardware address to zero
        packet->data.target_hardware_address.address[i] = 0;
    }
    packet->data.sender_ipv4_address.address = packet->data.target_ipv4_address.address = IPV4;
    return packet;
}

EthernetNSMFrame *CGuestAnnouncePackets::CreateIPv6Packet(USHORT * IPV6)
{
    ICMPv6PseudoHeader pseudo_header;
    EthernetNSMFrame *packet = (EthernetNSMFrame *)ParaNdis_AllocateMemory(m_Context, sizeof(EthernetNSMFrame));
    if (!packet)
    {
        DPrintf(0, "Error could not allocate buffer for arp packet!\n");
        return NULL;
    }
    packet->frame.ether_type = _byteswap_ushort(ETH_ETHER_TYPE_IPV6);
    for (UINT i = 0; i < ETH_HARDWARE_ADDRESS_SIZE; i++)
    {
        packet->frame.sender_hardware_address.address[i] = (UINT8)m_Context->CurrentMacAddress[i];
        packet->frame.target_hardware_address.address[i] = 0xFF;
    }
    packet->data.version_trafficclass_flowlabel = _byteswap_ulong(ETH_IPV6_VERSION_TRAFFICCONTROL_FLOWLABEL);
    packet->data.payload_length = _byteswap_ushort(sizeof(packet->data.nsm));
    pseudo_header.icmpv6_length = (UINT16) _byteswap_ushort(sizeof(packet->data.nsm));
    packet->data.next_header = pseudo_header.next_header = ETH_IPV6_ICMPV6_PROTOCOL;
    packet->data.hop_limit = 0xFF;
    for (UINT i = 0; i < ETH_IPV6_USHORT_ADDRESS_SIZE; i++)
    {
        packet->data.source_address.address[i] = pseudo_header.source_address.address[i] = 0x0;
        packet->data.destination_address.address[i] = packet->data.nsm.target_address.address[i] =
            pseudo_header.destination_address.address[i] = pseudo_header.nsm.target_address.address[i] = IPV6[i];
    }
    packet->data.nsm.type = pseudo_header.nsm.type = ETH_ICMPV6_TYPE_NSM;
    packet->data.nsm.code = pseudo_header.nsm.code = 0x0;
    packet->data.nsm.checksum = pseudo_header.nsm.checksum = 0x0;
    packet->data.nsm.reserved = pseudo_header.nsm.reserved = 0x0;
    packet->data.nsm.checksum = (CheckSumCalculator(&pseudo_header, sizeof(pseudo_header)));
    return packet;
}


VOID CGuestAnnouncePackets::CreateNBL(PVOID packet, UINT size, bool isIPV4)
{
    PMDL mdl = NdisAllocateMdl(m_Context->MiniportHandle, packet, size);
    if (!mdl)
    {
        DPrintf(0, "[%s] mdl allocation failed!\n", __FUNCTION__);
        NdisFreeMemory(packet, 0, 0);
        return;
    }
    PNET_BUFFER_LIST nbl = NdisAllocateNetBufferAndNetBufferList(m_Context->BufferListsPool, 0, 0, mdl, 0, size);
    if (!nbl)
    {
        DPrintf(0, "[%s] nbl allocation failed!\n", __FUNCTION__);
        NdisFreeMemory(packet, 0, 0);
        NdisFreeMdl(mdl);
        return;
    }
    nbl->SourceHandle = m_Context->MiniportHandle;
    CGuestAnnouncePacketHolder *PacketHolder = new (m_Context->MiniportHandle) CGuestAnnouncePacketHolder(nbl, m_Context->MiniportHandle, isIPV4);
    if (!PacketHolder)
    {
        DPrintf(0, "[%s] Packet holder allocation failed!\n", __FUNCTION__);
        NdisFreeNetBufferList(nbl);
        NdisFreeMdl(mdl);
        NdisFreeMemory(packet, 0, 0);
        return;
    }
    m_packets.Push(PacketHolder);
}

VOID CGuestAnnouncePackets::CreateNBL(UINT32 IPV4)
{
    EthernetArpFrame * packet = CreateIPv4Packet(IPV4);
    if (packet)
    {
        CreateNBL(packet, sizeof(EthernetArpFrame), true);
    }
}

VOID CGuestAnnouncePackets::CreateNBL(USHORT *IPV6)
{
    EthernetNSMFrame * packet = CreateIPv6Packet(IPV6);
    if (packet)
    {
        CreateNBL(packet, sizeof(EthernetNSMFrame), false);
    }
}

VOID CGuestAnnouncePackets::SendNBLs()
{
    m_packets.ForEach([this](CGuestAnnouncePacketHolder* GratARPPacket)
    {
        auto OriginalNBL = GratARPPacket->GetNBL();
        auto NewNBL = NdisAllocateCloneNetBufferList(OriginalNBL, m_Context->BufferListsPool, NULL, cloneFlags);
        if (NewNBL)
        {
            GratARPPacket->AddRef();
            NewNBL->SourceHandle = m_Context->MiniportHandle;
            NewNBL->MiniportReserved[0] = GratARPPacket;
            NewNBL->ParentNetBufferList = OriginalNBL;
            NdisInterlockedIncrement(&OriginalNBL->ChildRefCount);
            DPrintf(1, "[%s] ChildRefCount %d", __FUNCTION__, OriginalNBL->ChildRefCount);
            ParaNdis6_SendNBLInternal(m_Context, NewNBL, 0, 0);
        }
    });
}

void CGuestAnnouncePackets::NblCompletionCallback(PNET_BUFFER_LIST NBL)
{
    CGuestAnnouncePacketHolder *GratARPPacket = (CGuestAnnouncePacketHolder *)NBL->MiniportReserved[0];
    if (GratARPPacket)
    {
        auto OriginalNBL = GratARPPacket->GetNBL();
        NETKVM_ASSERT(NBL->ParentNetBufferList == OriginalNBL);
        NdisInterlockedDecrement(&OriginalNBL->ChildRefCount);
        DPrintf(1, "[%s] ChildRefCount %d, Child %s", __FUNCTION__, OriginalNBL->ChildRefCount, (NBL->ParentNetBufferList == OriginalNBL) ? "OK" : "Bad");
        NdisFreeCloneNetBufferList(NBL, cloneFlags);
        GratARPPacket->Release();
    }
}

bool CallCompletionForNBL(PARANDIS_ADAPTER * pContext, PNET_BUFFER_LIST NBL)
{
    return !(NBL->SourceHandle == pContext->MiniportHandle && ParaNdis_CountNBLs(NBL) == 1);
}
