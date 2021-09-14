/*
 * This file contains implementation of protocol for binding
 * virtio-net and SRIOV device
 *
 * Copyright (c) 2020 Red Hat, Inc.
 * Copyright (c) 2020 Oracle Corporation
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "ParaNdis_Protocol.h"
#include "ParaNdis-SM.h"
#include "ParaNdis-Oid.h"
#include "Trace.h"

#if NDIS_SUPPORT_NDIS630

#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_Protocol.tmh"
#endif

#define GUESS_VERSION(a, b) (a) = max((a), (b))

static PVOID ParaNdis_ReferenceBinding(PARANDIS_ADAPTER *pContext)
{
    return pContext->m_StateMachine.ReferenceSriovBinding();
}

static void ParaNdis_DereferenceBinding(PARANDIS_ADAPTER *pContext)
{
    pContext->m_StateMachine.DereferenceSriovBinding();
}

static void TracePnpId(PDEVICE_OBJECT pdo, PARANDIS_ADAPTER *Adapter)
{
    if (!pdo)
    {
        return;
    }
    ULONG length = 0;
    NTSTATUS status = IoGetDeviceProperty(pdo, DevicePropertyHardwareID, 0, NULL, &length);
    if (!length)
    {
        return;
    }
    char *buffer = (char *)ParaNdis_AllocateMemory(Adapter, length);
    if (!buffer)
    {
        return;
    }
    status = IoGetDeviceProperty(pdo, DevicePropertyHardwareID, length, buffer, &length);
    if (NT_SUCCESS(status))
    {
        TraceNoPrefix(0, "PnpId %S", (LPCWSTR)buffer);
    }
    NdisFreeMemory(buffer, 0, 0);
}

class CInternalNblEntry : public CNdisAllocatable<CInternalNblEntry, 'NORP'>
{
public:
    CInternalNblEntry(PNET_BUFFER_LIST Nbl) : m_Nbl(Nbl), m_KeepReserved(Nbl->MiniportReserved[0]) { }
    bool Match(PNET_BUFFER_LIST Nbl)
    {
        if (Nbl == m_Nbl)
        {
            Nbl->MiniportReserved[0] = m_KeepReserved;
            TraceNoPrefix(0, "[%s] restored %p:%p\n", __FUNCTION__, Nbl, m_KeepReserved);
            return true;
        }
        return false;
    }
private:
    PVOID m_KeepReserved;
    PNET_BUFFER_LIST m_Nbl;
    DECLARE_CNDISLIST_ENTRY(CInternalNblEntry);
};

class CAdapterEntry : public CNdisAllocatable<CAdapterEntry, '1ORP'>
{
public:
    CAdapterEntry(PARANDIS_ADAPTER *pContext) : m_Adapter(pContext), m_Binding(NULL)
    {
        NdisMoveMemory(m_MacAddress, pContext->CurrentMacAddress, sizeof(m_MacAddress));
    }
    CAdapterEntry(PVOID Binding, const UINT8 *MacAddress) : m_Adapter(NULL), m_Binding(Binding)
    {
        NdisMoveMemory(m_MacAddress, MacAddress, sizeof(m_MacAddress));
    }
    bool MatchMac(UCHAR *MacAddress) const
    {
        bool diff;
        ETH_COMPARE_NETWORK_ADDRESSES_EQ_SAFE(MacAddress, m_MacAddress, &diff);
        return !diff;
    }
    void NotifyAdapterRemoval()
    {
        Notifier(m_Binding, NotifyEvent::Removal);
    }
    void NotifyAdapterArrival()
    {
        Notifier(m_Binding, NotifyEvent::Arrival, m_Adapter);
    }
    void NotifyAdapterDetach()
    {
        Notifier(m_Binding, NotifyEvent::Detach);
    }
    PARANDIS_ADAPTER *m_Adapter;
    PVOID m_Binding;
private:
    enum class NotifyEvent
    {
        Arrival,
        Removal,
        Detach
    };
    UINT8 m_MacAddress[ETH_HARDWARE_ADDRESS_SIZE];
    static void Notifier(PVOID Binding, NotifyEvent, PARANDIS_ADAPTER *Adapter = NULL);
    DECLARE_CNDISLIST_ENTRY(CAdapterEntry);
};

class COidWrapper : public CNdisAllocatable <COidWrapper, '2ORP'>
{
public:
    COidWrapper(NDIS_REQUEST_TYPE RequestType, NDIS_OID oid)
    {
        NdisZeroMemory(&m_Request, sizeof(m_Request));
        PVOID data = this;
        NdisMoveMemory(m_Request.SourceReserved, &data, sizeof(PVOID));
        m_Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
        m_Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
        m_Request.Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
        m_Request.PortNumber = NDIS_DEFAULT_PORT_NUMBER;
        m_Request.RequestType = RequestType;
        m_Request.DATA.QUERY_INFORMATION.Oid = oid;
    }
    virtual void Complete(NDIS_STATUS status)
    {
        LPCSTR reqType = m_Request.RequestType == NdisRequestSetInformation ? "Set" : "Query";
        if (status) {
            TraceNoPrefix(0, "[%s] (%s)%s = %X\n", __FUNCTION__, reqType, ParaNdis_OidName(m_Request.DATA.SET_INFORMATION.Oid), status);
        }
        else {
            TraceNoPrefix(0, "[%s] (%s)%s OK, %d bytes\n", __FUNCTION__, reqType,
                ParaNdis_OidName(m_Request.DATA.SET_INFORMATION.Oid),
                m_Request.DATA.QUERY_INFORMATION.BytesWritten);
        }
        m_Status = status;
        m_Event.Notify();
    }
    void Run(NDIS_HANDLE Owner)
    {
        bool synchronous = m_Synchronous;
        NDIS_STATUS status = NdisOidRequest(Owner, &m_Request);
        if (status == NDIS_STATUS_PENDING)
        {
            // it's risky to touch the object, it may be async
            if (synchronous)
            {
                Wait();
            }
        }
        else
        {
            Complete(status);
        }
    }
    NDIS_STATUS Status() const { return m_Status; }
    NDIS_OID_REQUEST m_Request;
    virtual ~COidWrapper() { }
protected:
    bool m_Synchronous = true;
private:
    void Wait()
    {
        m_Event.Wait();
    }
    CNdisEvent m_Event;
    NDIS_STATUS m_Status;
};

class COidWrapperAsync : public COidWrapper
{
public:
    COidWrapperAsync(PARANDIS_ADAPTER *Adapter, NDIS_REQUEST_TYPE RequestType, NDIS_OID oid, PVOID buffer, ULONG length) :
        COidWrapper(RequestType, oid),
        m_Length(length),
        m_Adapter(Adapter),
        m_Handle(Adapter->MiniportHandle)
    {
        m_Synchronous = false;
        if (m_Length)
        {
            m_Data = NdisAllocateMemoryWithTagPriority(m_Handle, length, DataTag, NormalPoolPriority);
        }
        if (m_Data)
        {
            NdisMoveMemory(m_Data, buffer, length);
            m_Request.DATA.SET_INFORMATION.InformationBuffer = m_Data;
            m_Request.DATA.SET_INFORMATION.InformationBufferLength = m_Length;
        }
    }
    void Complete(NDIS_STATUS status) override
    {
        __super::Complete(status);
        Destroy(this, m_Handle);
    }
    ~COidWrapperAsync()
    {
        ParaNdis_DereferenceBinding(m_Adapter);
        if (m_Data)
        {
            NdisFreeMemoryWithTagPriority(m_Handle, m_Data, DataTag);
        }
    }
private:
    const ULONG DataTag = '3ORP';
    PVOID m_Data = NULL;
    ULONG m_Length = 0;
    NDIS_HANDLE m_Handle;
    PARANDIS_ADAPTER *m_Adapter;
};

static void PrintOffload(LPCSTR caller, NDIS_OFFLOAD& current)
{
    TraceNoPrefix(0, "[%s] Offload data v%d:\n", caller, current.Header.Revision);
    NDIS_TCP_LARGE_SEND_OFFLOAD_V2& lso = current.LsoV2;
    TraceNoPrefix(0, "LSOv2: v4 e:%X seg:%d, v6 e:%X seg:%d iph:%d opt:%d\n",
        lso.IPv4.Encapsulation, lso.IPv4.MaxOffLoadSize,
        lso.IPv6.Encapsulation, lso.IPv6.MaxOffLoadSize, lso.IPv6.IpExtensionHeadersSupported, lso.IPv6.TcpOptionsSupported);
    NDIS_TCP_IP_CHECKSUM_OFFLOAD& cso = current.Checksum;
    TraceNoPrefix(0, "Checksum4 RX: ip %d%c, tcp %d%c, udp %d\n",
        cso.IPv4Receive.IpChecksum, cso.IPv4Receive.IpOptionsSupported ? '+' : ' ',
        cso.IPv4Receive.TcpChecksum, cso.IPv4Receive.TcpOptionsSupported ? '+' : ' ',
        cso.IPv4Receive.UdpChecksum);
    TraceNoPrefix(0, "Checksum4 TX: ip %d%c, tcp %d%c, udp %d\n",
        cso.IPv4Transmit.IpChecksum, cso.IPv4Transmit.IpOptionsSupported ? '+' : ' ',
        cso.IPv4Transmit.TcpChecksum, cso.IPv4Transmit.TcpOptionsSupported ? '+' : ' ',
        cso.IPv4Transmit.UdpChecksum);
    TraceNoPrefix(0, "Checksum6 RX: ipx %d, tcp %d%c, udp %d\n",
        cso.IPv6Receive.IpExtensionHeadersSupported,
        cso.IPv6Receive.TcpChecksum, cso.IPv6Receive.TcpOptionsSupported ? '+' : ' ',
        cso.IPv6Receive.UdpChecksum);
    TraceNoPrefix(0, "Checksum6 TX: ipx %d, tcp %d%c, udp %d\n",
        cso.IPv6Transmit.IpExtensionHeadersSupported,
        cso.IPv6Transmit.TcpChecksum, cso.IPv6Transmit.TcpOptionsSupported ? '+' : ' ',
        cso.IPv6Transmit.UdpChecksum);
    if (current.Header.Revision > NDIS_OFFLOAD_REVISION_2) {
        NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD& rsc = current.Rsc;
        TraceNoPrefix(0, "RSCv4 %d, RSCv6 %d\n", rsc.IPv4.Enabled, rsc.IPv6.Enabled);
    }
}

class CParaNdisProtocol;

class CProtocolBinding : public CNdisAllocatable<CProtocolBinding, 'TORP'>, public CRefCountingObject
{
public:
    CProtocolBinding(CParaNdisProtocol& Protocol, NDIS_HANDLE BindContext) :
        m_Protocol(Protocol),
        m_BindContext(BindContext),
        m_Status(NDIS_STATUS_ADAPTER_NOT_OPEN),
        m_BindingHandle(NULL),
        m_BoundAdapter(NULL)
    {
        TraceNoPrefix(0, "[%s] %p\n", __FUNCTION__, this);
    }
    ~CProtocolBinding()
    {
        TraceNoPrefix(0, "[%s] %p\n", __FUNCTION__, this);
    }
    NDIS_STATUS Bind(PNDIS_BIND_PARAMETERS BindParameters);
    NDIS_STATUS Unbind(NDIS_HANDLE UnbindContext);
    void QueryCapabilities(PNDIS_BIND_PARAMETERS);
    void Complete(NDIS_STATUS Status)
    {
        m_Status = Status;
        m_Event.Notify();
    }
    void OidComplete(PNDIS_OID_REQUEST OidRequest, NDIS_STATUS Status)
    {
        PVOID data;
        NdisMoveMemory(&data, OidRequest->SourceReserved, sizeof(PVOID));
        COidWrapper *pOid = (COidWrapper *)data;
        pOid->Complete(Status);
    }
    void OnReceive(PNET_BUFFER_LIST Nbls, ULONG NofNbls, ULONG Flags);
    void OnSendCompletion(PNET_BUFFER_LIST Nbls, ULONG Flags);
    // called under protocol mutex from NETKVM's Halt()
    void OnAdapterHalted()
    {
        if (m_Started)
        {
            m_TxStateMachine.Stop();
            m_RxStateMachine.Stop();
        }
        m_Started = false;
        CPassiveSpinLockedContext lock(m_OpStateLock);
        m_BoundAdapter = NULL;
    }
    // called under protocol mutex
    // when VFIO adapter enters operational state
    void OnAdapterAttached()
    {
        ULONG waitTime = 100; //ms
        TraceNoPrefix(0, "[%s] %p\n", __FUNCTION__, m_BoundAdapter);
        if (m_BoundAdapter->MulticastData.nofMulticastEntries)
        {
            SetOid(OID_802_3_MULTICAST_LIST, m_BoundAdapter->MulticastData.MulticastList,
                ETH_ALEN * m_BoundAdapter->MulticastData.nofMulticastEntries);
        }
        m_BoundAdapter->m_StateMachine.NotifyBindSriov(this);

        ParaNdis_PropagateOid(m_BoundAdapter, OID_GEN_RECEIVE_SCALE_PARAMETERS, NULL, 0);
        ParaNdis_PropagateOid(m_BoundAdapter, OID_TCP_OFFLOAD_PARAMETERS, NULL, 0);
        ParaNdis_PropagateOid(m_BoundAdapter, OID_OFFLOAD_ENCAPSULATION, NULL, 0);

        QueryCurrentRSS();

        m_TxStateMachine.Start();
        m_RxStateMachine.Start();

        TraceNoPrefix(0, "[%s] Wait %d ms until the adapter finally restarted\n", __FUNCTION__, waitTime);
        NdisMSleep(waitTime * 1000);

        m_BoundAdapter->bSuppressLinkUp = false;
        ParaNdis_SynchronizeLinkState(m_BoundAdapter);
        m_Started = true;
        SetOid(OID_GEN_CURRENT_PACKET_FILTER, &m_BoundAdapter->PacketFilter, sizeof(m_BoundAdapter->PacketFilter));
    }
    // called under protocol mutex
    // when netkvm adapter comes and binding present
    // when binding to VFIO comes and netkvm adapter present
    void OnAdapterFound(PARANDIS_ADAPTER *Adapter)
    {
        TraceNoPrefix(0, "[%s] %p\n", __FUNCTION__, Adapter);
        TracePnpId(m_Pdo, Adapter);
        if (!CheckCompatibility(Adapter))
        {
            return;
        }
        m_BoundAdapter = Adapter;
        if (!m_Operational)
        {
            ULONG millies = 100;
            NdisMSleep(millies * 1000);
        }
        if (m_Operational)
        {
            OnAdapterAttached();
        } else {
            TraceNoPrefix(0, "[%s] WARNING: the adapter is not in operational state!\n", __FUNCTION__);
        }
    }

    // called under protocol mutex before close VFIO adapter
    void OnAdapterDetach()
    {
        if (m_Started)
        {
            m_TxStateMachine.Stop();
            m_RxStateMachine.Stop();
            m_BoundAdapter->m_StateMachine.NotifyUnbindSriov();
            ParaNdis_SendGratuitousArpPacket(m_BoundAdapter);
            m_Started = false;
        }
        CPassiveSpinLockedContext lock(m_OpStateLock);
        m_BoundAdapter = NULL;
    }
    bool CheckCompatibility(const PARANDIS_ADAPTER *Adapter)
    {
        if (m_Capabilies.MtuSize != Adapter->MaxPacketSize.nMaxDataSize)
        {
            TraceNoPrefix(0, "[%s] MTU size is not compatible: %d != %d\n", __FUNCTION__,
                m_Capabilies.MtuSize, Adapter->MaxPacketSize.nMaxDataSize);
            return false;
        }
        return true;
    }
    void OnStatusIndication(PNDIS_STATUS_INDICATION StatusIndication)
    {
        switch (StatusIndication->StatusCode)
        {
            case NDIS_STATUS_OPER_STATUS:
                {
                    NDIS_OPER_STATE *st = (NDIS_OPER_STATE *)StatusIndication->StatusBuffer;
                    if (StatusIndication->StatusBufferSize >= sizeof(*st))
                    {
                        bool state = st->OperationalStatus == NET_IF_OPER_STATUS_UP;
                        if (state != m_Operational)
                        {
                            m_Operational = state;
                            TraceNoPrefix(0, "[%s] the adapter is %sperational\n",
                                __FUNCTION__, m_Operational ? "O" : "NOT O");
                            CPassiveSpinLockedContext lock(m_OpStateLock);
                            if (m_BoundAdapter)
                            {
                                auto wi = new (m_BindingHandle)COperationWorkItem(this, state, m_BoundAdapter->MiniportHandle);
                                if (wi && !wi->Run())
                                {
                                    wi->Destroy(wi, m_BindingHandle);
                                }
                            }
                        }
                    }
                }
                break;
            case NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG:
                {
                    NDIS_OFFLOAD *o = (NDIS_OFFLOAD *)StatusIndication->StatusBuffer;
                    PrintOffload(__FUNCTION__, *o);
                    if (o->Header.Revision > NDIS_OFFLOAD_REVISION_3)
                    {
                        UCHAR knownMinor = m_Capabilies.NdisMinor;
                        GUESS_VERSION(m_Capabilies.NdisMinor, 30);
                        if (m_Capabilies.NdisMinor != knownMinor)
                        {
                            TraceNoPrefix(0, "[%s] Best guess for NDIS revision: 6.%d\n", __FUNCTION__, m_Capabilies.NdisMinor);
                        }
                    }
                }
                break;
            case NDIS_STATUS_LINK_STATE:
                {
                    const char *states[] = { "Unknown", "Connected", "Disconnected" };
                    NDIS_LINK_STATE *ls = (NDIS_LINK_STATE *)StatusIndication->StatusBuffer;
                    ULONG state = ls->MediaConnectState;
                    TraceNoPrefix(0, "[%s] link state %s(%d)\n", __FUNCTION__,
                        state <= MediaConnectStateDisconnected ? states[state] : "Invalid",
                        state);
                }
                break;
            default:
                TraceNoPrefix(0, "[%s] code %X\n", __FUNCTION__, StatusIndication->StatusCode);
                break;
        }
    }
    void OnPnPEvent(PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
    {
        TraceNoPrefix(0, "[%s] event %X\n", __FUNCTION__, NetPnPEventNotification->NetPnPEvent.NetEvent);
    }
    CFlowStateMachine m_RxStateMachine;
    CFlowStateMachine m_TxStateMachine;
    bool Send(PNET_BUFFER_LIST Nbl, ULONG Count);
    void ReturnNbls(PNET_BUFFER_LIST pNBL, ULONG numNBLs, ULONG flags);
    void SetOidAsync(ULONG oid, PVOID data, ULONG size);
    void SetOid(ULONG oid, PVOID data, ULONG size);
    void SetRSS();
    void SetOffloadEncapsulation();
    void SetOffloadParameters();
private:
    void QueryCurrentOffload();
    void QueryCurrentRSS();
    bool QueryOid(ULONG oid, PVOID data, ULONG size);
    void OnOpStateChange(bool State);
    void CompleteInternalNbl(PNET_BUFFER_LIST Nbl)
    {
        m_InternalNbls.ForEachDetachedIf(
            [Nbl](CInternalNblEntry *e)
            {
                return e->Match(Nbl);
            },
            [&](CInternalNblEntry *e)
            {
                e->Destroy(e, m_BindingHandle);
                CGuestAnnouncePackets::NblCompletionCallback(Nbl);
                return false;
            }
        );
    }
    void OnLastReferenceGone() override;
    CParaNdisProtocol& m_Protocol;
    NDIS_HANDLE m_BindContext;
    NDIS_HANDLE m_BindingHandle;
    // set under protocol mutex
    // clear under protocol mutex and m_OpStateLock
    PARANDIS_ADAPTER *m_BoundAdapter;
    CNdisEvent m_Event;
    bool       m_Operational = false;
    // set and clear under protocol mutex
    bool       m_Started = false;
    PDEVICE_OBJECT m_Pdo = NULL;
    NDIS_STATUS m_Status;
    struct
    {
        UCHAR NdisMinor;
        ULONG MtuSize;
        struct {
            ULONG queues;
            ULONG vectors;
            ULONG tableSize;
            bool  v4;
            bool  v6;
            bool  v6ex;
        } rss;
        struct
        {
            bool v4;
            bool v6;
        } rsc;
        struct
        {
            struct
            {
                ULONG maxPayload;
                ULONG minSegments;
            } v4;
            struct
            {
                ULONG maxPayload;
                ULONG minSegments;
                bool  extHeaders;
                bool  tcpOptions;
            } v6;
        } lsov2;
        struct
        {
            bool ip;
            bool tcp;
            bool udp;
        } checksumTx;
        struct
        {
            bool ip;
            bool tcp;
            bool udp;
        } checksumRx;
    } m_Capabilies = {};
    CNdisSpinLock m_OpStateLock;
    CNdisList<CInternalNblEntry, CLockedAccess, CNonCountingObject> m_InternalNbls;
    class COperationWorkItem : public CNdisAllocatable<COperationWorkItem, 'IWRP'>
    {
    public:
        COperationWorkItem(CProtocolBinding  *Binding, bool State, NDIS_HANDLE AdapterHandle);
        ~COperationWorkItem();
        bool Run()
        {
            if (m_Handle)
            {
                NdisQueueIoWorkItem(m_Handle, [](PVOID WorkItemContext, NDIS_HANDLE NdisIoWorkItemHandle)
                {
                    COperationWorkItem *wi = (COperationWorkItem *)WorkItemContext;
                    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);
                    wi->Fired();
                }, this);
            }
            return m_Handle;
        }
        void Fired()
        {
            m_Binding->OnOpStateChange(m_State);
            Destroy(this, m_Binding->m_BindingHandle);
        }
    private:
        NDIS_HANDLE       m_Handle;
        CProtocolBinding  *m_Binding;
        bool              m_State;
    };
    friend class COperationWorkItem;
};

static CParaNdisProtocol *ProtocolData = NULL;

class CParaNdisProtocol :
    public CNdisAllocatable<CParaNdisProtocol, 'TORP'>,
    public CRefCountingObject
{
public:
    CParaNdisProtocol(NDIS_HANDLE DriverHandle) :
        m_DriverHandle(DriverHandle),
        m_ProtocolHandle(NULL)
    {
    }
    NDIS_STATUS RegisterDriver()
    {
        NDIS_PROTOCOL_DRIVER_CHARACTERISTICS pchs = {};
        pchs.Name = NDIS_STRING_CONST("netkvmp");
        //pchs.Name = NDIS_STRING_CONST("NDISPROT");
        pchs.Header.Type = NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
        pchs.Header.Revision = NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2;
        pchs.Header.Size = NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2;
        pchs.MajorNdisVersion = 6;
        pchs.MinorNdisVersion = 30;
        pchs.MajorDriverVersion = (UCHAR)(PARANDIS_MAJOR_DRIVER_VERSION & 0xFF);
        pchs.MinorDriverVersion = (UCHAR)(PARANDIS_MINOR_DRIVER_VERSION & 0xFF);
        pchs.BindAdapterHandlerEx = [](
            _In_ NDIS_HANDLE ProtocolDriverContext,
            _In_ NDIS_HANDLE BindContext,
            _In_ PNDIS_BIND_PARAMETERS BindParameters)
        {
            TraceNoPrefix(0, "[BindAdapterHandlerEx] binding %p\n", BindContext);
            ParaNdis_ProtocolActive();
            return ((CParaNdisProtocol *)ProtocolDriverContext)->OnBindAdapter(BindContext, BindParameters);
        };
        pchs.OpenAdapterCompleteHandlerEx = [](
            _In_ NDIS_HANDLE ProtocolBindingContext,
            _In_ NDIS_STATUS Status)
        {
            TraceNoPrefix(0, "[OpenAdapterCompleteHandlerEx] %X, ctx %p\n", Status, ProtocolBindingContext);
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            binding->Complete(Status);
        };
        pchs.UnbindAdapterHandlerEx = [](
            _In_ NDIS_HANDLE UnbindContext,
            _In_ NDIS_HANDLE ProtocolBindingContext)
        {
            TraceNoPrefix(0, "[UnbindAdapterHandlerEx] ctx %p, binding %p\n", UnbindContext, ProtocolBindingContext);
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            return binding->Unbind(UnbindContext);
        };
        pchs.CloseAdapterCompleteHandlerEx = [](
            _In_ NDIS_HANDLE ProtocolBindingContext)
        {
            TraceNoPrefix(0, "[CloseAdapterCompleteHandlerEx] ctx %p\n", ProtocolBindingContext);
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            binding->Complete(NDIS_STATUS_SUCCESS);
        };
        pchs.SendNetBufferListsCompleteHandler = [](
            _In_  NDIS_HANDLE             ProtocolBindingContext,
            _In_  PNET_BUFFER_LIST        NetBufferList,
            _In_  ULONG                   SendCompleteFlags)
        {
            TraceNoPrefix(2, "[SendNetBufferListsCompleteHandler] ctx %p\n", ProtocolBindingContext);
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            binding->OnSendCompletion(NetBufferList, SendCompleteFlags);
        };
        pchs.ReceiveNetBufferListsHandler = [](
            _In_  NDIS_HANDLE             ProtocolBindingContext,
            _In_  PNET_BUFFER_LIST        NetBufferLists,
            _In_  NDIS_PORT_NUMBER        PortNumber,
            _In_  ULONG                   NumberOfNetBufferLists,
            _In_  ULONG                   ReceiveFlags)
        {
            UNREFERENCED_PARAMETER(PortNumber);
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            binding->OnReceive(NetBufferLists, NumberOfNetBufferLists, ReceiveFlags);
        };
        pchs.OidRequestCompleteHandler = [](
            _In_  NDIS_HANDLE             ProtocolBindingContext,
            _In_  PNDIS_OID_REQUEST       OidRequest,
            _In_  NDIS_STATUS             Status
            )
        {
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            binding->OidComplete(OidRequest, Status);
        };
        pchs.UninstallHandler = []()
        {
        };
        pchs.StatusHandlerEx = [](
            _In_  NDIS_HANDLE             ProtocolBindingContext,
            _In_  PNDIS_STATUS_INDICATION StatusIndication
            )
        {
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            binding->OnStatusIndication(StatusIndication);
        };
        pchs.NetPnPEventHandler = [](
            _In_  NDIS_HANDLE                 ProtocolBindingContext,
            _In_  PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification
            )
        {
            CProtocolBinding *binding = (CProtocolBinding *)ProtocolBindingContext;
            binding->OnPnPEvent(NetPnPEventNotification);
            return NDIS_STATUS_SUCCESS;
        };
        NDIS_STATUS status = NdisRegisterProtocolDriver(this, &pchs, &m_ProtocolHandle);
        TraceNoPrefix(0, "[%s] Registering protocol %wZ = %X\n", __FUNCTION__, &pchs.Name, status);
        return status;
    }
    ~CParaNdisProtocol()
    {
        if (m_ProtocolHandle)
        {
            TraceNoPrefix(0, "[%s] Deregistering protocol\n", __FUNCTION__);
            NdisDeregisterProtocolDriver(m_ProtocolHandle);
            TraceNoPrefix(0, "[%s] Deregistering protocol done\n", __FUNCTION__);
        }
    }
    NDIS_STATUS OnBindAdapter(_In_ NDIS_HANDLE BindContext, _In_ PNDIS_BIND_PARAMETERS BindParameters)
    {
        CProtocolBinding *binding = new (m_DriverHandle)CProtocolBinding(*this, BindContext);
        if (!binding)
        {
            return NDIS_STATUS_RESOURCES;
        }
        NDIS_STATUS status = binding->Bind(BindParameters);
        return status;
    }
    void AddAdapter(PARANDIS_ADAPTER *pContext)
    {
        LPCSTR func = __FUNCTION__;
        bool Done = false;

        CMutexLockedContext protect(m_Mutex);

        // find existing entry, if binding exists
        m_Adapters.ForEach([&](CAdapterEntry *e)
        {
            if (e->MatchMac(pContext->CurrentMacAddress))
            {
                if (!e->m_Adapter)
                {
                    TraceNoPrefix(0, "[%s] found entry %p for adapter %p\n", func, e, pContext);
                    e->m_Adapter = pContext;
                    e->NotifyAdapterArrival();
                    Done = true;
                    return false;
                }
                else
                {
                    TraceNoPrefix(0, "[%s] duplicated MAC entry %p for adapter %p, existing %p\n",
                        func, e, pContext, e->m_Adapter);
                }
            }
            return true;
        });
        if (Done)
        {
            return;
        }

        // create a new one, if binding does not exists
        CAdapterEntry *e = new (m_DriverHandle)CAdapterEntry(pContext);
        if (e)
        {
            UCHAR *mac = pContext->CurrentMacAddress;
            TraceNoPrefix(0, "[%s] new entry %p for adapter %p, mac %02X-%02X-%02X-%02X-%02X-%02X\n",
                func, e, pContext, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            m_Adapters.PushBack(e);
        }
    }
    bool RemoveAdapter(PARANDIS_ADAPTER *pContext)
    {
        LPCSTR func = __FUNCTION__;

        CMutexLockedContext protect(m_Mutex);

        m_Adapters.ForEach([&](CAdapterEntry *e)
        {
            if (e->m_Adapter != pContext) {
                return true;
            }
            e->m_Adapter = NULL;
            if (e->m_Binding) {
                TraceNoPrefix(0, "[%s] bound entry %p for adapter %p\n", func, e, pContext);
                e->NotifyAdapterRemoval();
            } else {
                TraceNoPrefix(0, "[%s] unbound entry %p for adapter %p\n", func, e, pContext);
                m_Adapters.Remove(e);
                e->Destroy(e, m_DriverHandle);
            }
            return false;
        });
        bool bNoMore = true;
        m_Adapters.ForEach([&](CAdapterEntry *e)
        {
            if (e->m_Adapter)
            {
                TraceNoPrefix(0, "[%s] still present entry %p for adapter %p\n", func, e, e->m_Adapter);
                bNoMore = false;
            }
        });
        if (bNoMore)
        {
            TraceNoPrefix(0, "[%s] no more adapters\n", func);
        }
        return bNoMore;
    }
    bool FindAdapterPdo(PDEVICE_OBJECT Pdo)
    {
        bool found = false;
        m_Adapters.ForEach([&](CAdapterEntry *e)
        {
            if (e->m_Adapter) {
                PDEVICE_OBJECT pdo = NULL;
                NdisMGetDeviceProperty(e->m_Adapter->MiniportHandle, &pdo, NULL, NULL, NULL, NULL);
                if (pdo == Pdo) {
                    found = true;
                    return false;
                }
            }
            return true;
        });
        return found;
    }
    void AddBinding(UCHAR *MacAddress, PVOID Binding)
    {
        LPCSTR func = __FUNCTION__;
        bool Done = false;

        CMutexLockedContext protect(m_Mutex);

        m_Adapters.ForEach([&](CAdapterEntry *e)
        {
            if (!e->MatchMac(MacAddress)) {
                return true;
            }
            if (e->m_Binding) {
                TraceNoPrefix(0, "[%s] already present binding %p with adapter %p\n",
                    func, e->m_Binding, e->m_Adapter);
                Done = true;
                return false;
            }
            e->m_Binding = Binding;
            e->NotifyAdapterArrival();
            Done = true;
            return false;
        });
        if (Done) {
            return;
        }
        CAdapterEntry *e = new (m_DriverHandle)CAdapterEntry(Binding, MacAddress);
        if (e)
        {
            TraceNoPrefix(0, "[%s] new entry %p for binding %p, mac %02X-%02X-%02X-%02X-%02X-%02X\n",
                func, e, Binding, MacAddress[0], MacAddress[1], MacAddress[2], MacAddress[3], MacAddress[4], MacAddress[5]);
            m_Adapters.PushBack(e);
        }
    }
    void RemoveBinding(PVOID Binding)
    {
        LPCSTR func = __FUNCTION__;

        CMutexLockedContext protect(m_Mutex);

        m_Adapters.ForEach([&](CAdapterEntry *e)
        {
            if (e->m_Binding != Binding) {
                return true;
            }
            if (e->m_Adapter) {
                TraceNoPrefix(0, "[%s] bound entry %p for binding %p\n", func, e, Binding);
                e->NotifyAdapterDetach();
                e->m_Binding = NULL;
            } else {
                TraceNoPrefix(0, "[%s] unbound entry %p for binding %p\n", func, e, Binding);
                // the list uses mutex for sync, so we can use 'Remove' here
                m_Adapters.Remove(e);
                e->Destroy(e, m_DriverHandle);
            }
            return false;
        });
    }
    NDIS_HANDLE DriverHandle() const { return m_DriverHandle; }
    NDIS_HANDLE ProtocolHandle() const { return m_ProtocolHandle; }
    operator CMutexProtectedAccess& () { return m_Mutex; }
private:
    CNdisList<CAdapterEntry, CRawAccess, CCountingObject> m_Adapters;
    // there are procedures with several operations on the list,
    // we need to protected them together, so use separate mutex
    CMutexProtectedAccess m_Mutex;
    NDIS_HANDLE m_DriverHandle;
    NDIS_HANDLE m_ProtocolHandle;
    void OnLastReferenceGone() override
    {
        Destroy(this, m_DriverHandle);
    }
};

NDIS_STATUS ParaNdis_ProtocolInitialize(NDIS_HANDLE DriverHandle)
{
    if (ProtocolData)
    {
        return NDIS_STATUS_SUCCESS;
    }
    ProtocolData = new (DriverHandle) CParaNdisProtocol(DriverHandle);
    NDIS_STATUS status = ProtocolData->RegisterDriver();
    if (status != NDIS_STATUS_SUCCESS)
    {
        ProtocolData->Destroy(ProtocolData, DriverHandle);
        ProtocolData = NULL;
    }
    return status;
}

void ParaNdis_ProtocolRegisterAdapter(PARANDIS_ADAPTER *pContext)
{
    if (!ProtocolData)
        return;
    ProtocolData->AddAdapter(pContext);
}

void ParaNdis_ProtocolUnregisterAdapter(PARANDIS_ADAPTER *pContext, bool UnregisterOnLast)
{
    if (!ProtocolData)
        return;
    if (ProtocolData->RemoveAdapter(pContext) && UnregisterOnLast)
    {
        ProtocolData->Release();
        ProtocolData = NULL;
    }
}

NDIS_STATUS CProtocolBinding::Bind(PNDIS_BIND_PARAMETERS BindParameters)
{
    NDIS_OPEN_PARAMETERS openParams = {};
    NDIS_MEDIUM medium = NdisMedium802_3;
    UINT mediumIndex;
    NET_FRAME_TYPE frameTypes[2] = { NDIS_ETH_TYPE_802_1X, NDIS_ETH_TYPE_802_1Q };

    AddRef();

    m_Pdo = BindParameters->PhysicalDeviceObject;
    if (m_Protocol.FindAdapterPdo(m_Pdo))
    {
        TraceNoPrefix(0, "[%s] rejected binding to NETKVM instance\n", __FUNCTION__);
        Unbind(m_BindContext);
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    openParams.Header.Type = NDIS_OBJECT_TYPE_OPEN_PARAMETERS;
    openParams.Header.Revision = NDIS_OPEN_PARAMETERS_REVISION_1;
    openParams.Header.Size = NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1;
    openParams.AdapterName = BindParameters->AdapterName;
    openParams.MediumArray = &medium;
    openParams.MediumArraySize = 1;
    openParams.SelectedMediumIndex = &mediumIndex;
    openParams.FrameTypeArray = frameTypes;
    openParams.FrameTypeArraySize = ARRAYSIZE(frameTypes);

    NDIS_STATUS status = NdisOpenAdapterEx(m_Protocol.ProtocolHandle(), this, &openParams, m_BindContext, &m_BindingHandle);

    if (status == STATUS_SUCCESS)
    {
        TraceNoPrefix(0, "[%s] %p done\n", __FUNCTION__, this);
        m_Status = STATUS_SUCCESS;
    }
    else if (status == STATUS_PENDING)
    {
        m_Event.Wait();
        status = m_Status;
    }
    if (!NT_SUCCESS(status))
    {
        TraceNoPrefix(0, "[%s] %p, failed %X\n", __FUNCTION__, this, status);
        Unbind(m_BindContext);
    }
    else
    {
        ULONG val = 0;
        // make the VF silent
        SetOid(OID_GEN_CURRENT_PACKET_FILTER, &val, sizeof(val));
        QueryCapabilities(BindParameters);
        QueryCurrentOffload();
        QueryCurrentRSS();
        m_Protocol.AddBinding(BindParameters->CurrentMacAddress, this);
    }

    Release();

    return status;
}

NDIS_STATUS CProtocolBinding::Unbind(NDIS_HANDLE UnbindContext)
{
    NDIS_STATUS status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(UnbindContext);

    if (m_BindingHandle)
    {
        ULONG valueNone = 0;

        m_Protocol.RemoveBinding(this);

        SetOid(OID_GEN_CURRENT_PACKET_FILTER, &valueNone, sizeof(valueNone));
        SetOid(OID_802_3_MULTICAST_LIST, NULL, 0);

        m_Event.Clear();
        status = NdisCloseAdapterEx(m_BindingHandle);
        switch (status)
        {
            case NDIS_STATUS_PENDING:
                m_Event.Wait();
                status = NDIS_STATUS_SUCCESS;
                break;
            case NDIS_STATUS_SUCCESS:
                break;
            default:
                TraceNoPrefix(0, "[%s] %p, failed %X\n", __FUNCTION__, this, status);
                return status;
        }
        m_BindingHandle = NULL;
    }
    Release();
    return status;
}

void CProtocolBinding::OnLastReferenceGone()
{
    Destroy(this, m_Protocol.DriverHandle());
}

void CProtocolBinding::OnReceive(PNET_BUFFER_LIST Nbls, ULONG NofNbls, ULONG Flags)
{
    // Flags may contain RESOURCES  (but why this should make difference?)
    bool bDrop = true;
    if (m_RxStateMachine.RegisterOutstandingItems(NofNbls))
    {
        if (m_BoundAdapter)
        {
            TraceNoPrefix(1, "[%s] %d NBLs\n", __FUNCTION__, NofNbls);
            NdisMIndicateReceiveNetBufferLists(
                m_BoundAdapter->MiniportHandle, Nbls,
                NDIS_DEFAULT_PORT_NUMBER, NofNbls, Flags);
        }
        else
        {
            // should never happen
            TraceNoPrefix(0, "[%s] ERROR: dropped %d NBLs\n", __FUNCTION__, NofNbls);
            ReturnNbls(Nbls, NofNbls, 0);
        }
        bDrop = false;
    }
    if (bDrop)
    {
        TraceNoPrefix(0, "[%s] dropped %d NBLs\n", __FUNCTION__, NofNbls);
        NdisReturnNetBufferLists(m_BindingHandle, Nbls,
            (Flags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL) ? NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
    }
}

void CProtocolBinding::ReturnNbls(PNET_BUFFER_LIST pNBL, ULONG numNBLs, ULONG flags)
{
    NdisReturnNetBufferLists(m_BindingHandle, pNBL, flags);
    m_RxStateMachine.UnregisterOutstandingItems(numNBLs);
}

void ParaNdis_ProtocolReturnNbls(PARANDIS_ADAPTER *pContext, PNET_BUFFER_LIST pNBL, ULONG numNBLs, ULONG flags)
{
    // ensure the adapter has a binding and if yes, reference it
    CProtocolBinding *pb = (CProtocolBinding *)ParaNdis_ReferenceBinding(pContext);
    if (pb)
    {
        TraceNoPrefix(1, "[%s] %d NBLs\n", __FUNCTION__, numNBLs);
        pb->ReturnNbls(pNBL, numNBLs, flags);
        ParaNdis_DereferenceBinding(pContext);
    }
    else
    {
        TraceNoPrefix(0, "[%s] ERROR: Can't return %d NBL\n", __FUNCTION__, numNBLs);
    }
}

void CProtocolBinding::QueryCurrentOffload()
{
    NDIS_OFFLOAD current;
    current.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
    current.Header.Revision = NDIS_OFFLOAD_REVISION_3;
    current.Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_3;
    if (!QueryOid(OID_TCP_OFFLOAD_CURRENT_CONFIG, &current, sizeof(current)))
    {
        return;
    }
    PrintOffload(__FUNCTION__, current);
}

void CProtocolBinding::QueryCurrentRSS()
{
    struct RSSQuery : public CNdisAllocatable<RSSQuery, 'QORP'>
    {
        RSSQuery()
        {
            RtlZeroMemory(&rsp, sizeof(rsp));
            rsp.Header.Type = NDIS_OBJECT_TYPE_RSS_PARAMETERS;
            rsp.Header.Revision = NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2;
            rsp.Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2;
        }
        NDIS_RECEIVE_SCALE_PARAMETERS rsp;
        UCHAR key[NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_1];
        UCHAR indirection[NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2];
    };
    auto current = new (m_Protocol.DriverHandle()) RSSQuery;
    if (!current) {
        return;
    }
    if (QueryOid(OID_GEN_RECEIVE_SCALE_PARAMETERS, current, sizeof(*current)))
    {
        TraceNoPrefix(0, "[%s] RSS hash info %X\n", __FUNCTION__, current->rsp.HashInformation);
    }
    current->Destroy(current, m_Protocol.DriverHandle());
}

bool CProtocolBinding::QueryOid(ULONG oid, PVOID data, ULONG size)
{
    COidWrapper *p = new (m_Protocol.DriverHandle()) COidWrapper(NdisRequestQueryInformation, oid);
    if (!p)
    {
        return false;
    }
    p->m_Request.DATA.SET_INFORMATION.InformationBuffer = data;
    p->m_Request.DATA.SET_INFORMATION.InformationBufferLength = size;
    p->Run(m_BindingHandle);
    NTSTATUS status = p->Status();
    p->Destroy(p, m_Protocol.DriverHandle());
    return NT_SUCCESS(status);
}

void CProtocolBinding::SetOid(ULONG oid, PVOID data, ULONG size)
{
    COidWrapper *p = new (m_Protocol.DriverHandle()) COidWrapper(NdisRequestSetInformation, oid);
    if (!p)
    {
        return;
    }
    p->m_Request.DATA.SET_INFORMATION.InformationBuffer = data;
    p->m_Request.DATA.SET_INFORMATION.InformationBufferLength = size;
    p->Run(m_BindingHandle);
    p->Destroy(p, m_Protocol.DriverHandle());
}

void CProtocolBinding::SetOidAsync(ULONG oid, PVOID data, ULONG size)
{
    COidWrapperAsync *p = new (m_Protocol.DriverHandle()) COidWrapperAsync(m_BoundAdapter, NdisRequestSetInformation, oid, data, size);
    if (!p)
    {
        return;
    }
    p->Run(m_BindingHandle);
}

static bool KeepSourceHandleInContext(PNET_BUFFER_LIST Nbl)
{
#if 0
    NDIS_STATUS status = NdisAllocateNetBufferListContext(Nbl, sizeof(NDIS_HANDLE), 0, '3ORP');
    if (!NT_SUCCESS(status))
    {
        TraceNoPrefix(0, "[%s] error %X", __FUNCTION__, status);
        return false;
    }
    *(PNDIS_HANDLE)NET_BUFFER_LIST_CONTEXT_DATA_START(Nbl) = Nbl->SourceHandle;
#else
    NET_BUFFER_LIST_INFO(Nbl, TcpRecvSegCoalesceInfo) = Nbl->SourceHandle;
#endif
    return true;
}

static void RetrieveSourceHandle(PNET_BUFFER_LIST start, PNET_BUFFER_LIST stopAt = NULL)
{
    PNET_BUFFER_LIST current = start;
    while (current && current != stopAt)
    {
#if 0
        current->SourceHandle = *(PNDIS_HANDLE)NET_BUFFER_LIST_CONTEXT_DATA_START(current);
        NdisFreeNetBufferListContext(current, sizeof(NDIS_HANDLE));
#else
        current->SourceHandle = NET_BUFFER_LIST_INFO(current, TcpRecvSegCoalesceInfo);
#endif
        current = NET_BUFFER_LIST_NEXT_NBL(current);
    }
}

bool CProtocolBinding::Send(PNET_BUFFER_LIST Nbl, ULONG Count)
{
    if (Nbl == NULL || !m_TxStateMachine.RegisterOutstandingItems(Count))
    {
        return false;
    }
    PNET_BUFFER_LIST current = Nbl;

    if (!CallCompletionForNBL(m_BoundAdapter, Nbl))
    {
        CInternalNblEntry *e = new (m_BindingHandle)CInternalNblEntry(Nbl);
        if (!e)
        {
            m_TxStateMachine.UnregisterOutstandingItems(Count);
            return false;
        }
        m_InternalNbls.PushBack(e);
        TraceNoPrefix(0, "[%s] internal NBL %p, reserved %p\n",
            __FUNCTION__, Nbl, Nbl->MiniportReserved[0]);
    }

    while (current)
    {
        if (!KeepSourceHandleInContext(current))
        {
            RetrieveSourceHandle(Nbl, current);
            if (!CallCompletionForNBL(m_BoundAdapter, Nbl))
            {
                CompleteInternalNbl(Nbl);
            }
            m_TxStateMachine.UnregisterOutstandingItems(Count);
            // TODO: can we transmit the packets via NETKVM???
            return false;
        }
        current->SourceHandle = m_BindingHandle;
        current = NET_BUFFER_LIST_NEXT_NBL(current);
    }
    TraceNoPrefix(1, "[%s] %d nbls\n", __FUNCTION__, Count);
    NdisSendNetBufferLists(m_BindingHandle, Nbl, NDIS_DEFAULT_PORT_NUMBER, 0);
    return true;
}

void CProtocolBinding::OnSendCompletion(PNET_BUFFER_LIST Nbls, ULONG Flags)
{
    PNET_BUFFER_LIST *pCurrent = &Nbls;
    PNET_BUFFER_LIST current = Nbls;
    ULONG errors = 0, count = 0;
    int level;

    RetrieveSourceHandle(Nbls);

    while (current)
    {
        count++;
        if (NET_BUFFER_LIST_STATUS(current) != NDIS_STATUS_SUCCESS)
            errors++;
        if (!CallCompletionForNBL(m_BoundAdapter, current))
        {
            // remove the NBL from the chain
            PNET_BUFFER_LIST toFree = current;
            TraceNoPrefix(0, "[%s] internal NBL %p, reserved %p\n", __FUNCTION__, toFree, toFree->MiniportReserved[0]);
            *pCurrent = NET_BUFFER_LIST_NEXT_NBL(current);
            NET_BUFFER_LIST_NEXT_NBL(toFree) = NULL;
            // return the NBL to the owner
            CompleteInternalNbl(toFree);
        }
        else
        {
            pCurrent = &NET_BUFFER_LIST_NEXT_NBL(*pCurrent);
        }
        current = *pCurrent;
    }

    level = errors ? 0 : 1;
    TraceNoPrefix(level, "[%s] %d nbls(%d errors)\n", __FUNCTION__, count, errors);

    if (Nbls)
    {
        NdisMSendNetBufferListsComplete(m_BoundAdapter->MiniportHandle, Nbls, Flags);
    }
    m_TxStateMachine.UnregisterOutstandingItems(count);
}

void CAdapterEntry::Notifier(PVOID Binding, NotifyEvent Event, PARANDIS_ADAPTER *Adapter)
{
    CProtocolBinding *pb = (CProtocolBinding *)Binding;
    if (!pb)
    {
        TraceNoPrefix(0, "[%s] No binding present, skip the notification\n", __FUNCTION__);
        return;
    }
    switch (Event)
    {
        case NotifyEvent::Arrival:
            pb->OnAdapterFound(Adapter);
            break;
        case NotifyEvent::Removal:
            pb->OnAdapterHalted();
            break;
        case NotifyEvent::Detach:
            pb->OnAdapterDetach();
            break;
        default:
            break;
    }
}

bool ParaNdis_ProtocolSend(PARANDIS_ADAPTER *pContext, PNET_BUFFER_LIST Nbl)
{
    // ensure the adapter has a binding and if yes, reference it
    CProtocolBinding *pb = (CProtocolBinding *)ParaNdis_ReferenceBinding(pContext);
    if (!pb)
    {
        return false;
    }
    ULONG count = ParaNdis_CountNBLs(Nbl);
    bool b = pb->Send(Nbl, count);
    ParaNdis_DereferenceBinding(pContext);
    return b;
}

VOID ParaNdis_PropagateOid(PARANDIS_ADAPTER *pContext, NDIS_OID oid, PVOID buffer, UINT length)
{
    CProtocolBinding *pb = (CProtocolBinding *)ParaNdis_ReferenceBinding(pContext);
    if (!pb)
    {
        return;
    }
    switch (oid)
    {
        case OID_GEN_RECEIVE_SCALE_PARAMETERS:
            pb->SetRSS();
            break;
        case OID_OFFLOAD_ENCAPSULATION:
            pb->SetOffloadEncapsulation();
            break;
        case OID_TCP_OFFLOAD_PARAMETERS:
            pb->SetOffloadParameters();
            break;
        default:
            pb->SetOidAsync(oid, buffer, length);
            break;
    }
}

void CProtocolBinding::QueryCapabilities(PNDIS_BIND_PARAMETERS BindParameters)
{
    // if split enabled, it is NDIS 6.10 at least
    m_Capabilies.MtuSize = BindParameters->MtuSize;
    if (BindParameters->HDSplitCurrentConfig)
    {
        ULONG flags = BindParameters->HDSplitCurrentConfig->CurrentCapabilities;
        if (flags & NDIS_HD_SPLIT_CAPS_SUPPORTS_HEADER_DATA_SPLIT)
        {
            struct
            {
                bool ipv4opt;
                bool tcpopt;
                bool ipv6ext;
                ULONG maxHeader;
                ULONG backfill;
            } hds;
            hds.ipv4opt = flags & NDIS_HD_SPLIT_CAPS_SUPPORTS_IPV4_OPTIONS;
            hds.tcpopt = flags & NDIS_HD_SPLIT_CAPS_SUPPORTS_TCP_OPTIONS;
            hds.ipv6ext = flags & NDIS_HD_SPLIT_CAPS_SUPPORTS_IPV6_EXTENSION_HEADERS;
            hds.maxHeader = BindParameters->HDSplitCurrentConfig->MaxHeaderSize;
            hds.backfill = BindParameters->HDSplitCurrentConfig->BackfillSize;
            GUESS_VERSION(m_Capabilies.NdisMinor, 10);
            TraceNoPrefix(0, "[%s] HDS: ipv4opt:%d, ipv6ext:%d, tcpopt:%d, max header:%d, backfill %d\n",
                __FUNCTION__, hds.ipv4opt, hds.ipv6ext, hds.tcpopt, hds.maxHeader, hds.backfill);
        }
    }
    // If RSS has table size, it is 6.30 at least
    if (BindParameters->RcvScaleCapabilities)
    {
        ULONG flags = BindParameters->RcvScaleCapabilities->CapabilitiesFlags;
        m_Capabilies.rss.v4 = flags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4;
        m_Capabilies.rss.v6 = flags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6;
        m_Capabilies.rss.v6ex = flags & NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX;
        m_Capabilies.rss.queues = BindParameters->RcvScaleCapabilities->NumberOfReceiveQueues;
        m_Capabilies.rss.vectors = BindParameters->RcvScaleCapabilities->NumberOfInterruptMessages;
        if (flags & NDIS_RSS_CAPS_USING_MSI_X) {
            GUESS_VERSION(m_Capabilies.NdisMinor, 20);
        }
        if (BindParameters->RcvScaleCapabilities->Header.Revision > NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1)
        {
            GUESS_VERSION(m_Capabilies.NdisMinor, 30);
            m_Capabilies.rss.tableSize = BindParameters->RcvScaleCapabilities->NumberOfIndirectionTableEntries;
        }
        TraceNoPrefix(0, "[%s] RSS: v4:%d,v6:%d,v6ex:%d, queues:%d, vectors:%d, max table:%d\n", __FUNCTION__,
            m_Capabilies.rss.v4, m_Capabilies.rss.v6, m_Capabilies.rss.v6ex,
            m_Capabilies.rss.queues, m_Capabilies.rss.vectors, m_Capabilies.rss.tableSize);
    }
    else
    {
        TraceNoPrefix(0, "[%s] No RSS capabilies\n", __FUNCTION__);
    }
    // If RSC enabled, it is 6.30 at least
    if (BindParameters->DefaultOffloadConfiguration)
    {
        PNDIS_OFFLOAD doc = BindParameters->DefaultOffloadConfiguration;
        m_Capabilies.rsc.v4 = doc->Rsc.IPv4.Enabled;
        m_Capabilies.rsc.v6 = doc->Rsc.IPv6.Enabled;
        if (m_Capabilies.rsc.v4 || m_Capabilies.rsc.v6)
        {
            GUESS_VERSION(m_Capabilies.NdisMinor, 30);
        }
        m_Capabilies.lsov2.v4.maxPayload = doc->LsoV2.IPv4.MaxOffLoadSize;
        m_Capabilies.lsov2.v4.minSegments = doc->LsoV2.IPv4.MinSegmentCount;
        m_Capabilies.lsov2.v6.maxPayload = doc->LsoV2.IPv6.MaxOffLoadSize;
        m_Capabilies.lsov2.v6.minSegments = doc->LsoV2.IPv6.MinSegmentCount;
        m_Capabilies.lsov2.v6.extHeaders = doc->LsoV2.IPv6.IpExtensionHeadersSupported;
        m_Capabilies.lsov2.v6.tcpOptions = doc->LsoV2.IPv6.TcpOptionsSupported;
        TraceNoPrefix(0, "[%s] LSOv2: v4: min segments %d, max payload %d\n", __FUNCTION__,
            m_Capabilies.lsov2.v4.minSegments, m_Capabilies.lsov2.v4.maxPayload);
        TraceNoPrefix(0, "[%s] LSOv2: v6: min segments %d, max payload %d, tcp opt:%d, extHeaders:%d\n",
            __FUNCTION__,
            m_Capabilies.lsov2.v6.minSegments, m_Capabilies.lsov2.v6.maxPayload,
            m_Capabilies.lsov2.v6.tcpOptions, m_Capabilies.lsov2.v6.extHeaders);
        TraceNoPrefix(0, "[%s] RSC: v4:%d, v6:%d\n", __FUNCTION__,
            m_Capabilies.rsc.v4, m_Capabilies.rsc.v6);
    }
    else
    {
        TraceNoPrefix(0, "[%s] No offload capabilies\n", __FUNCTION__);
    }
    TraceNoPrefix(0, "[%s] Best guess for NDIS revision: 6.%d\n", __FUNCTION__, m_Capabilies.NdisMinor);
}

CProtocolBinding::COperationWorkItem::COperationWorkItem(CProtocolBinding  *Binding, bool State, NDIS_HANDLE AdapterHandle) :
    m_State(State), m_Binding(Binding)
{
    TraceNoPrefix(0, "[%s]\n", __FUNCTION__);
    m_Handle = NdisAllocateIoWorkItem(AdapterHandle);
    m_Binding->AddRef();
    m_Binding->m_Protocol.AddRef();
}

CProtocolBinding::COperationWorkItem::~COperationWorkItem()
{
    TraceNoPrefix(0, "[%s]\n", __FUNCTION__);
    if (m_Handle)
    {
        NdisFreeIoWorkItem(m_Handle);
    }
    m_Binding->m_Protocol.Release();
    m_Binding->Release();
}

void CProtocolBinding::OnOpStateChange(bool State)
{
    CMutexLockedContext protect(m_Protocol);
    if (State && !m_Started && m_BoundAdapter)
    {
        OnAdapterAttached();
    }
}

// Do not call this procedure directly, only via ParaNdis_PropagateOid
// as it is asynchronous and will call ParaNdis_DereferenceBinding
void CProtocolBinding::SetRSS()
{
    bool bSkip = !m_Capabilies.rss.queues;
    COidWrapperAsync *p = NULL;
    struct RSSSet : public CNdisAllocatable<RSSSet, 'SORP'>
    {
        NDIS_RECEIVE_SCALE_PARAMETERS rsp{};
        UCHAR key[NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_1]{};
        UCHAR indirection[NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2]{};
    };
    auto current = new (m_Protocol.DriverHandle()) RSSSet;
    bSkip = !current || bSkip;
    if (!bSkip) {
        RtlZeroMemory(current, sizeof(*current));
        current->rsp.Header.Type = NDIS_OBJECT_TYPE_RSS_PARAMETERS;
        current->rsp.Header.Revision = NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2;
        current->rsp.Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2;
        switch (m_BoundAdapter->RSSParameters.RSSMode)
        {
            case PARANDIS_RSS_MODE::PARANDIS_RSS_FULL:
            case PARANDIS_RSS_MODE::PARANDIS_RSS_HASHING:
                current->rsp.HashInformation = m_BoundAdapter->RSSParameters.ActiveHashingSettings.HashInformation;
                current->rsp.HashSecretKeyOffset = FIELD_OFFSET(RSSSet, key);
                current->rsp.HashSecretKeySize = m_BoundAdapter->RSSParameters.ActiveHashingSettings.HashSecretKeySize;
                RtlMoveMemory(current->key,
                    m_BoundAdapter->RSSParameters.ActiveHashingSettings.HashSecretKey,
                    current->rsp.HashSecretKeySize);
                current->rsp.IndirectionTableOffset = FIELD_OFFSET(RSSSet, indirection);
                current->rsp.IndirectionTableSize = m_BoundAdapter->RSSParameters.ActiveRSSScalingSettings.IndirectionTableSize;
                RtlMoveMemory(current->indirection,
                    m_BoundAdapter->RSSParameters.ActiveRSSScalingSettings.IndirectionTable,
                    current->rsp.IndirectionTableSize);
                break;
            default:
                current->rsp.Flags = NDIS_RSS_PARAM_FLAG_DISABLE_RSS;
                break;
        }
#if (NDIS_SUPPORT_NDIS680)
        current->rsp.HashInformation &= ~(NDIS_HASH_UDP_IPV4 | NDIS_HASH_UDP_IPV6 | NDIS_HASH_UDP_IPV6_EX);
#endif // (NDIS_SUPPORT_NDIS680)
        if (!m_Capabilies.rss.v6ex) {
            current->rsp.HashInformation &= ~(NDIS_HASH_IPV6_EX | NDIS_HASH_TCP_IPV6_EX);
        }
        if (!m_Capabilies.rss.v6) {
            current->rsp.HashInformation &= ~(NDIS_HASH_IPV6 | NDIS_HASH_TCP_IPV6);
        }
        p = new (m_Protocol.DriverHandle()) COidWrapperAsync(m_BoundAdapter, NdisRequestSetInformation, OID_GEN_RECEIVE_SCALE_PARAMETERS, current, sizeof(*current));
        if (!p){
            bSkip = true;
        }
    }
    if (!bSkip) {
        TraceNoPrefix(0, "[%s] Using hash info %X\n", __FUNCTION__, current->rsp.HashInformation);
        p->Run(m_BindingHandle);
    } else {
        ParaNdis_DereferenceBinding(m_BoundAdapter);
    }
    if (current) {
        current->Destroy(current, m_Protocol.DriverHandle());
    }
}

void CProtocolBinding::SetOffloadEncapsulation()
{
    bool bSkip = !m_Capabilies.lsov2.v4.maxPayload && !m_Capabilies.lsov2.v6.maxPayload;
    COidWrapperAsync *p = NULL;
    struct EncapSet : public CNdisAllocatable<EncapSet, 'EORP'>
    {
        NDIS_OFFLOAD_ENCAPSULATION e{};
    };
    auto current = new (m_Protocol.DriverHandle()) EncapSet;
    bSkip = !current || bSkip;
    if (!bSkip) {
        RtlZeroMemory(current, sizeof(*current));
        current->e.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION;
        current->e.Header.Revision = NDIS_OFFLOAD_ENCAPSULATION_REVISION_1;
        current->e.Header.Size = NDIS_SIZEOF_OFFLOAD_ENCAPSULATION_REVISION_1;
        current->e.IPv4.EncapsulationType = NDIS_ENCAPSULATION_IEEE_802_3;
        current->e.IPv6.EncapsulationType = NDIS_ENCAPSULATION_IEEE_802_3;
        if (m_BoundAdapter->bOffloadv4Enabled && m_Capabilies.lsov2.v4.maxPayload) {
            current->e.IPv4.Enabled = NDIS_OFFLOAD_SET_ON;
            current->e.IPv4.HeaderSize = m_BoundAdapter->Offload.ipHeaderOffset;
        } else {
            current->e.IPv4.Enabled = NDIS_OFFLOAD_SET_OFF;
            current->e.IPv4.HeaderSize = 0;
        }
        if (m_BoundAdapter->bOffloadv6Enabled && m_Capabilies.lsov2.v6.maxPayload) {
            current->e.IPv6.Enabled = NDIS_OFFLOAD_SET_ON;
            current->e.IPv6.HeaderSize = m_BoundAdapter->Offload.ipHeaderOffset;
        }
        else {
            current->e.IPv6.Enabled = NDIS_OFFLOAD_SET_OFF;
            current->e.IPv6.HeaderSize = 0;
        }

        p = new (m_Protocol.DriverHandle()) COidWrapperAsync(m_BoundAdapter, NdisRequestSetInformation, OID_OFFLOAD_ENCAPSULATION, current, sizeof(*current));
        if (!p) {
            bSkip = true;
        }
    }
    if (!bSkip) {
        TraceNoPrefix(0, "[%s] Using v4:%d, v6:%d\n", __FUNCTION__, current->e.IPv4.HeaderSize, current->e.IPv6.HeaderSize);
        p->Run(m_BindingHandle);
    }
    else {
        ParaNdis_DereferenceBinding(m_BoundAdapter);
    }
    if (current) {
        current->Destroy(current, m_Protocol.DriverHandle());
    }
}

static UCHAR ChecksumSetting(int Tx, int Rx)
{
    const UCHAR values[4] =
    {
        NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED,
        NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED,
        NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED,
        NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED
    };
    Tx = Tx ? 1 : 0;
    Rx = Rx ? 2 : 0;
    return values[Tx + Rx];
}

void CProtocolBinding::SetOffloadParameters()
{
    bool bSkip = false;
    COidWrapperAsync *p = NULL;
    struct OffloadParam : public CNdisAllocatable<OffloadParam, 'EORP'>
    {
        NDIS_OFFLOAD_PARAMETERS o{};
    };
    auto current = new (m_Protocol.DriverHandle()) OffloadParam;
    bSkip = !current || bSkip;
    if (!bSkip) {
        tOffloadSettingsFlags f = m_BoundAdapter->Offload.flags;
        RtlZeroMemory(current, sizeof(*current));
        current->o.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        current->o.Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_2;
        current->o.Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_2;
        if (m_Capabilies.NdisMinor >= 30) {
            current->o.Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_3;
            current->o.Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_3;
        }
        current->o.RscIPv4 = m_BoundAdapter->RSC.bIPv4Enabled ?
            NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED : NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED;
        current->o.RscIPv6 = m_BoundAdapter->RSC.bIPv6Enabled ?
            NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED : NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED;
        if (m_Capabilies.lsov2.v4.maxPayload) {
            current->o.LsoV2IPv4 = f.fTxLso ? NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED : NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED;
        }
        if (m_Capabilies.lsov2.v6.maxPayload) {
            current->o.LsoV2IPv6 = f.fTxLsov6 ? NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED : NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED;
        }
        current->o.IPv4Checksum = ChecksumSetting(f.fTxIPChecksum, f.fRxIPChecksum);
        current->o.TCPIPv4Checksum = ChecksumSetting(f.fTxTCPChecksum, f.fRxTCPChecksum);
        current->o.TCPIPv6Checksum = ChecksumSetting(f.fTxTCPv6Checksum, f.fRxTCPv6Checksum);
        current->o.UDPIPv4Checksum = ChecksumSetting(f.fTxUDPChecksum, f.fRxUDPChecksum);
        current->o.UDPIPv6Checksum = ChecksumSetting(f.fTxUDPv6Checksum, f.fRxUDPv6Checksum);

        p = new (m_Protocol.DriverHandle()) COidWrapperAsync(m_BoundAdapter, NdisRequestSetInformation, OID_TCP_OFFLOAD_PARAMETERS, current, sizeof(*current));
        if (!p) {
            bSkip = true;
        }
    }
    if (!bSkip) {
        TraceNoPrefix(0, "[%s] Using Rsc v4:%d, v6:%d\n", __FUNCTION__, current->o.RscIPv4, current->o.RscIPv6);
        TraceNoPrefix(0, "[%s] Using LsoV2 v4:%d, v6:%d\n", __FUNCTION__, current->o.LsoV2IPv4, current->o.LsoV2IPv6);
        p->Run(m_BindingHandle);
    }
    else {
        ParaNdis_DereferenceBinding(m_BoundAdapter);
    }
    if (current) {
        current->Destroy(current, m_Protocol.DriverHandle());
    }
}

#else

void ParaNdis_ProtocolUnregisterAdapter(PARANDIS_ADAPTER *, bool) { }
void ParaNdis_ProtocolRegisterAdapter(PARANDIS_ADAPTER *) { }
NDIS_STATUS ParaNdis_ProtocolInitialize(NDIS_HANDLE) { }
bool ParaNdis_ProtocolSend(PARANDIS_ADAPTER *, PNET_BUFFER_LIST) { return false; }
void ParaNdis_PropagateOid(PARANDIS_ADAPTER *, NDIS_OID, PVOID, UINT) { }
void ParaNdis_ProtocolReturnNbls(PARANDIS_ADAPTER *, PNET_BUFFER_LIST, ULONG, ULONG) { }

#endif //NDIS_SUPPORT_NDIS630
