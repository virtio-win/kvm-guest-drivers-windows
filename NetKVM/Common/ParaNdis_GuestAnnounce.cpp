#include "ndis56common.h"
#include "ParaNdis_GuestAnnounce.h"
#include "ParaNdis6_Driver.h"
#include "ethernetutils.h"

CGratARPPacketHolder::~CGratARPPacketHolder()
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

void CGratARPPacketHolder::OnLastReferenceGone()
{
    Destroy(this, m_handle);
}

VOID CGratuitousArpPackets::CreateNBL(UINT32 IPV4)
{
    EthernetArpFrame *packet = (EthernetArpFrame *) ParaNdis_AllocateMemory(m_Context, sizeof(EthernetArpFrame));
    if (!packet)
    {
        DPrintf(0, ("Error could not allocate buffer for arp packet!\n"));
        return;
    }
    packet->ether_type = _byteswap_ushort(ETH_ETHER_TYPE_ARP);
    packet->data.hardware_type = _byteswap_ushort(ETH_HARDWARE_TYPE);
    packet->data.protocol_type = _byteswap_ushort(ETH_IP_PROTOCOL_TYPE);
    packet->data.hardware_address_length = ETH_HARDWARE_ADDRESS_SIZE;
    packet->data.protocol_address_length = ETH_IPV4_ADDRESS_SIZE;
    packet->data.operation = _byteswap_ushort(ETH_ARP_OPERATION_TYPE_REQUEST);
    for (UINT i = 0; i < ETH_HARDWARE_ADDRESS_SIZE; i++)
    {
        packet->sender_hardware_address.address[i] = (UINT8) m_Context->CurrentMacAddress[i];
        packet->target_hardware_address.address[i] = 0xFF;
        packet->data.sender_hardware_address.address[i] = m_Context->CurrentMacAddress[i];
        packet->data.target_hardware_address.address[i] = 0xFF;
    }
    packet->data.sender_ipv4_address.address = packet->data.target_ipv4_address.address = IPV4;
    PMDL mdl = NdisAllocateMdl(m_Context->MiniportHandle, packet, sizeof(EthernetArpFrame));
    if (!mdl)
    {
        DPrintf(0, ("[%s] mdl allocation failed!\n", __FUNCTION__));
        NdisFreeMemory(packet, 0, 0);
        return;
    }
    PNET_BUFFER_LIST nbl = NdisAllocateNetBufferAndNetBufferList(m_Context->BufferListsPool, 0, 0, mdl, 0, sizeof(EthernetArpFrame));
    if (!nbl)
    {
        DPrintf(0, ("[%s] nbl allocation failed!\n", __FUNCTION__));
        NdisFreeMemory(packet, 0, 0);
        NdisFreeMdl(mdl);
        return;
    }
    nbl->SourceHandle = m_Context->MiniportHandle;
    CGratARPPacketHolder *GratARPPacket = new (m_Context->MiniportHandle) CGratARPPacketHolder(nbl, m_Context->MiniportHandle);
    nbl->NdisReserved[0] = GratARPPacket;
    m_packets.Push(GratARPPacket);
}

VOID CGratuitousArpPackets::SendNBLs()
{
    auto ctx = m_Context;
    m_packets.ForEach([ctx](CGratARPPacketHolder* GratARPPacket)
    {
        GratARPPacket->AddRef();
        ParaNdis6_SendNBLInternal(ctx, GratARPPacket->GetNBL(), 0, NDIS_SEND_FLAGS_DISPATCH_LEVEL);
    });
}

CGratARPPacketHolder *CGratuitousArpPackets::GetCGratArpPacketFromNBL(PNET_BUFFER_LIST NBL)
{
    return (CGratARPPacketHolder *)NBL->NdisReserved[0];
}

bool CallCompletionForNBL(PARANDIS_ADAPTER * pContext, PNET_BUFFER_LIST NBL)
{
    return !(NBL->SourceHandle == pContext->MiniportHandle && ParaNdis_CountNBLs(NBL) == 1);
}
