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

static PVOID ParaNdis_ReferenceBinding(PARANDIS_ADAPTER *pContext)
{
    return pContext->m_StateMachine.ReferenceSriovBinding();
}

static void ParaNdis_DereferenceBinding(PARANDIS_ADAPTER *pContext)
{
    pContext->m_StateMachine.DereferenceSriovBinding();
}

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
        Notifier(m_Binding, Removal);
    }
    void NotifyAdapterArrival()
    {
        Notifier(m_Binding, Arrival, m_Adapter);
    }
    void NotifyAdapterDetach()
    {
        Notifier(m_Binding, Detach);
    }
    PARANDIS_ADAPTER *m_Adapter;
    PVOID m_Binding;
private:
    enum NotifyEvent
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
        TraceNoPrefix(0, "[%s] %s\n", __FUNCTION__, ParaNdis_OidName(m_Request.DATA.SET_INFORMATION.Oid));
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
        ParaNdis_DereferenceBinding(m_Adapter);
        Destroy(this, m_Handle);
    }
    ~COidWrapperAsync()
    {
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
        TraceNoPrefix(0, "[%s] %p\n", __FUNCTION__, m_BoundAdapter);
        SetOid(OID_GEN_CURRENT_PACKET_FILTER, &m_BoundAdapter->PacketFilter, sizeof(m_BoundAdapter->PacketFilter));
        if (m_BoundAdapter->MulticastData.nofMulticastEntries)
        {
            SetOid(OID_802_3_MULTICAST_LIST, m_BoundAdapter->MulticastData.MulticastList,
                ETH_ALEN * m_BoundAdapter->MulticastData.nofMulticastEntries);
        }
        // TODO: some other OIDs?

        m_BoundAdapter->m_StateMachine.NotifyBindSriov(this);
        m_TxStateMachine.Start();
        m_RxStateMachine.Start();
        m_Started = true;
    }
    // called under protocol mutex
    // when netkvm adapter comes and binding present
    // when binding to VFIO comes and netkvm adapter present
    void OnAdapterFound(PARANDIS_ADAPTER *Adapter)
    {
        TraceNoPrefix(0, "[%s] %p\n", __FUNCTION__, Adapter);
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
private:
    void OnOpStateChange(bool State);
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
                TraceNoPrefix(0, "[%s] found entry %p for adapter %p\n", func, e, pContext);
                e->m_Adapter = pContext;
                e->NotifyAdapterArrival();
                Done = true;
                return false;
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
                TraceNoPrefix(0, "[%s] existing entry %p for adapter %p\n", func, e, pContext);
                bNoMore = false;
            }
        });
        if (bNoMore)
        {
            TraceNoPrefix(0, "[%s] no more adapters\n", func);
        }
        return bNoMore;
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

void ParaNdis_ProtocolInitialize(NDIS_HANDLE DriverHandle)
{
    if (ProtocolData)
    {
        return;
    }
    ProtocolData = new (DriverHandle) CParaNdisProtocol(DriverHandle);
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

    QueryCapabilities(BindParameters);

    if (NT_SUCCESS(status))
    {
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
    if (!m_TxStateMachine.RegisterOutstandingItems(Count))
    {
        return false;
    }
    PNET_BUFFER_LIST current = Nbl;
    while (current)
    {
        if (!KeepSourceHandleInContext(current))
        {
            RetrieveSourceHandle(Nbl, current);
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

static ULONG CountNblsAndErrors(PNET_BUFFER_LIST NBL, ULONG& errors)
{
    ULONG result;
    for (result = 0, errors = 0; NBL != nullptr; NBL = NET_BUFFER_LIST_NEXT_NBL(NBL), result++)
    {
        if (NET_BUFFER_LIST_STATUS(NBL) != NDIS_STATUS_SUCCESS)
            errors++;
    }
    return result;
}

void CProtocolBinding::OnSendCompletion(PNET_BUFFER_LIST Nbls, ULONG Flags)
{
    ULONG errors;
    ULONG count = CountNblsAndErrors(Nbls, errors);
    TraceNoPrefix(errors != 0 ? 0 : 1, "[%s] %d nbls(%d errors)\n", __FUNCTION__, count, errors);
    RetrieveSourceHandle(Nbls);
    NdisMSendNetBufferListsComplete(m_BoundAdapter->MiniportHandle, Nbls, Flags);
    m_TxStateMachine.UnregisterOutstandingItems(count);
}

void CAdapterEntry::Notifier(PVOID Binding, NotifyEvent Event, PARANDIS_ADAPTER *Adapter)
{
    CProtocolBinding *pb = (CProtocolBinding *)Binding;
    switch (Event)
    {
        case Arrival:
            pb->OnAdapterFound(Adapter);
            break;
        case Removal:
            pb->OnAdapterHalted();
            break;
        case Detach:
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
    pb->SetOidAsync(oid, buffer, length);
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
            m_Capabilies.NdisMinor = 10;
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
        if (BindParameters->RcvScaleCapabilities->Header.Revision > NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1)
        {
            m_Capabilies.NdisMinor = 30;
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
            m_Capabilies.NdisMinor = 30;
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

#else

void ParaNdis_ProtocolUnregisterAdapter(PARANDIS_ADAPTER *, bool) { }
void ParaNdis_ProtocolRegisterAdapter(PARANDIS_ADAPTER *) { }
void ParaNdis_ProtocolInitialize(NDIS_HANDLE) { }
bool ParaNdis_ProtocolSend(PARANDIS_ADAPTER *, PNET_BUFFER_LIST) { return false; }
void ParaNdis_PropagateOid(PARANDIS_ADAPTER *, NDIS_OID, PVOID, UINT) { }
void ParaNdis_ProtocolReturnNbls(PARANDIS_ADAPTER *, PNET_BUFFER_LIST, ULONG, ULONG) { }

#endif //NDIS_SUPPORT_NDIS630
