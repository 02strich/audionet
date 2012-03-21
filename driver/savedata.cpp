/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    savedata.cpp

Abstract:
    Implementation of MSVAD data saving class.

    To save the playback data to disk, this class maintains a circular data
    buffer, associated frame structures and worker items to save frames to
    disk.
    Each frame structure represents a portion of buffer. When that portion
    of frame is full, a workitem is scheduled to save it to disk.



--*/
#pragma warning (disable : 4127)

#include "audionet.h"
#include "savedata.h"

//=============================================================================
// Defines
//=============================================================================
#define DEFAULT_FRAME_COUNT         5
#define DEFAULT_FRAME_SIZE          PAGE_SIZE
#define DEFAULT_BUFFER_SIZE         DEFAULT_FRAME_SIZE * DEFAULT_FRAME_COUNT

#define MAX_WORKER_ITEM_COUNT       1

//=============================================================================
// Statics
//=============================================================================
ULONG CSaveData::m_ulStreamId = 0;

// Client-level callback table
const WSK_CLIENT_DISPATCH WskSampleClientDispatch = {
    MAKE_WSK_VERSION(1, 0), // This sample uses WSK version 1.0
    0, // Reserved
    NULL // WskClientEvent callback is not required in WSK version 1.0
};

//=============================================================================
// Helper Functions
//=============================================================================
// IRP completion routine used for synchronously waiting for completion
NTSTATUS WskSampleSyncIrpCompletionRoutine(__in PDEVICE_OBJECT Reserved, __in PIRP Irp, __in PVOID Context) {    
    PKEVENT compEvent = (PKEVENT)Context;
    
    UNREFERENCED_PARAMETER(Reserved);
    UNREFERENCED_PARAMETER(Irp);
    
    KeSetEvent(compEvent, 2, FALSE);    

    return STATUS_MORE_PROCESSING_REQUIRED;
}

#pragma code_seg("PAGE")
//=============================================================================
// CSaveData
//=============================================================================

//=============================================================================
CSaveData::CSaveData() : m_pDataBuffer(NULL), m_ulFrameCount(DEFAULT_FRAME_COUNT), m_ulBufferSize(DEFAULT_BUFFER_SIZE), m_ulFrameSize(DEFAULT_FRAME_SIZE), m_ulBufferPtr(0), m_ulFramePtr(0), m_fFrameUsed(NULL), m_fWriteDisabled(FALSE), m_bInitialized(FALSE) {
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::CSaveData]"));
    
    WSK_CLIENT_NPI   wskClientNpi;
    
    // new stream
    m_ulStreamId++;
    
    // allocate work items for this stream
    InitializeWorkItems(GetDeviceObject());
    
    // get us an IRP
    m_irp = IoAllocateIrp(1, FALSE);

    // initialize io completion sychronization event
    KeInitializeEvent(&m_syncEvent, SynchronizationEvent, FALSE);
    
    // Register with WSK.
    wskClientNpi.ClientContext = NULL;
    wskClientNpi.Dispatch = &WskSampleClientDispatch;
    WskRegister(&wskClientNpi, &m_wskSampleRegistration);
    
} // CSaveData

//=============================================================================
CSaveData::~CSaveData() {
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::~CSaveData]"));

    // frees the work items
    for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++) {
        if (m_pWorkItems[i].WorkItem!=NULL) {
            IoFreeWorkItem(m_pWorkItems[i].WorkItem);
            m_pWorkItems[i].WorkItem = NULL;
        }
    }

    // close socket
    if(m_socket) {
        IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
        IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
        ((PWSK_PROVIDER_BASIC_DISPATCH)m_socket->Dispatch)->WskCloseSocket(m_socket, m_irp);
        KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
    }
    
    // Deregister with WSK. This call will wait until all the references to
    // the WSK provider NPI are released and all the sockets are closed. Note
    // that if the worker thread has not started yet, then when it eventually
    // starts, its WskCaptureProviderNPI call will fail and the work queue
    // will be flushed and cleaned up properly.
    WskDeregister(&m_wskSampleRegistration);

    // free irp
    IoFreeIrp(m_irp);
    
    if (m_waveFormat) {
        ExFreePoolWithTag(m_waveFormat, MSVAD_POOLTAG);
    }

    if (m_fFrameUsed) {
        ExFreePoolWithTag(m_fFrameUsed, MSVAD_POOLTAG);
    }

    if (m_pDataBuffer) {
        ExFreePoolWithTag(m_pDataBuffer, MSVAD_POOLTAG);
        IoFreeMdl(m_pDataMdl);
    }
} // CSaveData

//=============================================================================
void CSaveData::DestroyWorkItems(void) {
    PAGED_CODE();
    
    DPF_ENTER(("[CSaveData::DestroyWorkItems]"));

    if (m_pWorkItems) {
        ExFreePoolWithTag(m_pWorkItems, MSVAD_POOLTAG);
        m_pWorkItems = NULL;
    }

} // DestroyWorkItems

//=============================================================================
void CSaveData::Disable(BOOL fDisable) {
    PAGED_CODE();

    m_fWriteDisabled = fDisable;
} // Disable

//=============================================================================
NTSTATUS CSaveData::SetDeviceObject(IN PDEVICE_OBJECT DeviceObject) {
    PAGED_CODE();

    ASSERT(DeviceObject);

    NTSTATUS ntStatus = STATUS_SUCCESS;
    
    m_pDeviceObject = DeviceObject;
    return ntStatus;
}

//=============================================================================
PDEVICE_OBJECT CSaveData::GetDeviceObject(void) {
    PAGED_CODE();

    return m_pDeviceObject;
}

#pragma code_seg()
//=============================================================================
PSAVEWORKER_PARAM CSaveData::GetNewWorkItem(void) {
    LARGE_INTEGER timeOut = { 0 };
    NTSTATUS      ntStatus;

    DPF_ENTER(("[CSaveData::GetNewWorkItem]"));
    
    for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++) {
        ntStatus = KeWaitForSingleObject(&m_pWorkItems[i].EventDone, Executive, KernelMode, FALSE, &timeOut);
        if (STATUS_SUCCESS == ntStatus) {
            if (m_pWorkItems[i].WorkItem)
                return &(m_pWorkItems[i]);
            else
                return NULL;
        }
    }

    return NULL;
} // GetNewWorkItem

#pragma code_seg("PAGE")
//=============================================================================
NTSTATUS CSaveData::Initialize(const wchar_t* cServerName) {
    PAGED_CODE();

    NTSTATUS          ntStatus = STATUS_SUCCESS;

    DPF_ENTER(("[CSaveData::Initialize]"));
    
    // copy over servername
    RtlCopyMemory(m_cServerName, cServerName, sizeof(m_cServerName));    

    // Allocate memory for data buffer.
    if (NT_SUCCESS(ntStatus)) {
        m_pDataBuffer = (PBYTE) ExAllocatePoolWithTag(NonPagedPool, m_ulBufferSize, MSVAD_POOLTAG);
        if (!m_pDataBuffer) {
            DPF(D_TERSE, ("[Could not allocate memory for Saving Data]"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // Allocate MDL for the data buffer
    if (NT_SUCCESS(ntStatus)) {
        m_pDataMdl = IoAllocateMdl(m_pDataBuffer, m_ulBufferSize, FALSE, FALSE, NULL);
        if(m_pDataMdl == NULL) {
            DPF(D_TERSE, ("[Failed to allocate MDL]"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            MmBuildMdlForNonPagedPool(m_pDataMdl);
        }
    }    
    
    // Allocate memory for frame usage flags
    if (NT_SUCCESS(ntStatus)) {
        m_fFrameUsed = (PBOOL) ExAllocatePoolWithTag(NonPagedPool, m_ulFrameCount * sizeof(BOOL), MSVAD_POOLTAG);
        if (!m_fFrameUsed) {
            DPF(D_TERSE, ("[Could not allocate memory for frame flags]"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // Zero the page usage flags
    if (NT_SUCCESS(ntStatus)) {
        RtlZeroMemory(m_fFrameUsed, m_ulFrameCount * sizeof(BOOL));
    }
    
    // Initialize the spinlock to synchronize access to the frames
    KeInitializeSpinLock(&m_FrameInUseSpinLock);

    return ntStatus;
} // Initialize

//=============================================================================
NTSTATUS CSaveData::InitializeWorkItems(IN PDEVICE_OBJECT DeviceObject) {
    PAGED_CODE();

    ASSERT(DeviceObject);
    
    NTSTATUS ntStatus = STATUS_SUCCESS;

    DPF_ENTER(("[CSaveData::InitializeWorkItems]"));

    m_pWorkItems = (PSAVEWORKER_PARAM) ExAllocatePoolWithTag(NonPagedPool, sizeof(SAVEWORKER_PARAM) * MAX_WORKER_ITEM_COUNT, MSVAD_POOLTAG);
    if (m_pWorkItems) {
        for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++) {
            m_pWorkItems[i].WorkItem = IoAllocateWorkItem(DeviceObject);
            if(m_pWorkItems[i].WorkItem == NULL) {
              return STATUS_INSUFFICIENT_RESOURCES;
            }
            
            KeInitializeEvent(&m_pWorkItems[i].EventDone, NotificationEvent, TRUE);
        }
    } else {
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

    return ntStatus;
} // InitializeWorkItems

//=============================================================================
IO_WORKITEM_ROUTINE SaveFrameWorkerCallback;

VOID SaveFrameWorkerCallback(PDEVICE_OBJECT pDeviceObject, IN  PVOID  Context) {
    UNREFERENCED_PARAMETER(pDeviceObject);

    PAGED_CODE();

    ASSERT(Context);

    PSAVEWORKER_PARAM pParam = (PSAVEWORKER_PARAM) Context;
    PCSaveData        pSaveData;

    DPF_ENTER(("[SaveFrameWorkerCallback ulFrameNo=%d ulDataSize=%d]", pParam->ulFrameNo, pParam->ulDataSize));

    ASSERT(pParam->pSaveData);
    ASSERT(pParam->pSaveData->m_fFrameUsed);

    if (pParam->WorkItem) {
        pSaveData = pParam->pSaveData;
        pSaveData->SendData(pParam->ulOffset, pParam->ulDataSize);
        
        InterlockedExchange((LONG *)&(pSaveData->m_fFrameUsed[pParam->ulFrameNo]), FALSE);
    }

    KeSetEvent(&pParam->EventDone, 0, FALSE);
} // SaveFrameWorkerCallback

#pragma code_seg()
//=============================================================================
void CSaveData::CreateSocket(void) {
    NTSTATUS            status;
    WSK_PROVIDER_NPI    pronpi;
    PSOCKADDR           locaddr;
    SOCKADDR_IN         locaddr4 = { AF_INET,  RtlUshortByteSwap(4009), 0, 0};
    SOCKADDR_IN6        locaddr6 = { AF_INET6, RtlUshortByteSwap(4009), 0, IN6ADDR_ANY_INIT, 0};
    
    // server name lookup
    UNICODE_STRING      uniNodeName;
    PADDRINFOEXW        results;

    DPF_ENTER(("[CSaveData::CreateSocket] server name: %ws", m_cServerName));
    
    // capture WSK provider
    status = WskCaptureProviderNPI(&m_wskSampleRegistration, WSK_INFINITE_WAIT, &pronpi);
    if(!NT_SUCCESS(status)){
        DPF(D_TERSE, ("Failed to capture provider NPI: 0x%X\n", status));
        return;
    }
    
    // convert server name to UNICODE_STRING
    RtlInitUnicodeString(&uniNodeName, m_cServerName);
    
    // resolve configured server name
    IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
    IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);    
    pronpi.Dispatch->WskGetAddressInfo (
        pronpi.Client,
        &uniNodeName,
        NULL,
        NS_ALL,
        NULL,   // Provider
        NULL,
        &results, 
        NULL,   // OwningProcess
        NULL,   // OwningThread
        m_irp);
    KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
    
    DPF(D_TERSE, ("WskGetAddressInfo: %x", m_irp->IoStatus.Status));
    if (!NT_SUCCESS(m_irp->IoStatus.Status)) {
        DPF(D_TERSE, ("WskGetAddressInfo failed: %x.", m_irp->IoStatus.Status));
        
        // release the provider again, as we are finished with it
        WskReleaseProviderNPI(&m_wskSampleRegistration);
        
        return;
    } else if (results->ai_family == AF_INET) {
        // copy remote address and set port
        RtlCopyMemory(&m_sServerAddr, results->ai_addr, sizeof(SOCKADDR_IN));
        ((PSOCKADDR_IN)&m_sServerAddr)->sin_port = RtlUshortByteSwap(4010);
        
        // create IPv4 socket
        locaddr = (PSOCKADDR) &locaddr4;
    } else if (results->ai_family == AF_INET6) {
        RtlCopyMemory(&m_sServerAddr, results->ai_addr, sizeof(SOCKADDR_IN6));
        ((PSOCKADDR_IN6)&m_sServerAddr)->sin6_port = RtlUshortByteSwap(4010);
        
        // create IPv6 socket
        locaddr = (PSOCKADDR) &locaddr6;
    } else {
        DPF(D_TERSE, ("WskGetAddressInfo did not find IPv4 or IPv6 address."));
        
        // release the provider again, as we are finished with it
        WskReleaseProviderNPI(&m_wskSampleRegistration);
        
        return;
    }
    pronpi.Dispatch->WskFreeAddressInfo(pronpi.Client, results);
    
    DPF(D_TERSE, ("ss_family: %x", m_sServerAddr.ss_family));
    
    // create socket
    IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
    IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);    
    pronpi.Dispatch->WskSocket(
        pronpi.Client,
        m_sServerAddr.ss_family,
        SOCK_DGRAM,
        IPPROTO_UDP,
        WSK_FLAG_DATAGRAM_SOCKET,
        NULL, // socket context
        NULL, // dispatch
        NULL, // Process
        NULL, // Thread
        NULL, // SecurityDescriptor
        m_irp);
    KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
    
    DPF(D_TERSE, ("WskSocket: %x", m_irp->IoStatus.Status));
    
    if (!NT_SUCCESS(m_irp->IoStatus.Status)) {
        DPF(D_TERSE, ("Failed to create socket: %x", m_irp->IoStatus.Status));
        
        if(m_socket) {
            IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
            IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
            ((PWSK_PROVIDER_BASIC_DISPATCH)m_socket->Dispatch)->WskCloseSocket(m_socket, m_irp);
            KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
        }
        
        // release the provider again, as we are finished with it
        WskReleaseProviderNPI(&m_wskSampleRegistration);
        
        return;
    }
    
    // save the socket
    m_socket = (PWSK_SOCKET)m_irp->IoStatus.Information;
    
    // release the provider again, as we are finished with it
    WskReleaseProviderNPI(&m_wskSampleRegistration);

    // bind the socket
    IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
    IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
    status = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)(m_socket->Dispatch))->WskBind(m_socket, locaddr, 0, m_irp);	
    KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
    
    DPF(D_TERSE, ("WskBind: %x", m_irp->IoStatus.Status));
    
    if (!NT_SUCCESS(m_irp->IoStatus.Status)) {
        DPF(D_TERSE, ("Failed to bind socket: %x", m_irp->IoStatus.Status));
        if(m_socket) {
            IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
            IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
            ((PWSK_PROVIDER_BASIC_DISPATCH)m_socket->Dispatch)->WskCloseSocket(m_socket, m_irp);
            KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
        }
        
        return;
    }
}

//=============================================================================
void CSaveData::SendData(ULONG offset, ULONG length) {
    WSK_BUF             wskbuf;

    DPF_ENTER(("[CSaveData::SendData offset=%d length=%d]", offset, length));
    
    // initialize transport if not done yet
    if (!m_bInitialized) {
        this->CreateSocket();        
        m_bInitialized = TRUE;
        
        return;
    }
    
    wskbuf.Mdl = m_pDataMdl;
    wskbuf.Offset = offset;
    wskbuf.Length = length;
    
    if (m_socket) {
        IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
        IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
        ((PWSK_PROVIDER_DATAGRAM_DISPATCH)(m_socket->Dispatch))->WskSendTo(m_socket, &wskbuf, 0, (PSOCKADDR)&m_sServerAddr, 0, NULL, m_irp);
        KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
        
        DPF(D_TERSE, ("WskSendTo: %x", m_irp->IoStatus.Status));
    }
}

#pragma code_seg("PAGE")
//=============================================================================
NTSTATUS CSaveData::SetDataFormat(IN PKSDATAFORMAT pDataFormat) {
    PAGED_CODE();
    NTSTATUS ntStatus = STATUS_SUCCESS;
 
    DPF_ENTER(("[CSaveData::SetDataFormat]"));

    ASSERT(pDataFormat);

    PWAVEFORMATEX pwfx = NULL;

    if (IsEqualGUIDAligned(pDataFormat->Specifier, KSDATAFORMAT_SPECIFIER_DSOUND)) {
        pwfx = &(((PKSDATAFORMAT_DSOUND) pDataFormat)->BufferDesc.WaveFormatEx);
    } else if (IsEqualGUIDAligned(pDataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)) {
        pwfx = &((PKSDATAFORMAT_WAVEFORMATEX) pDataFormat)->WaveFormatEx;
    }

    if (pwfx) {
        // Free the previously allocated waveformat
        if (m_waveFormat) {
            ExFreePoolWithTag(m_waveFormat, MSVAD_POOLTAG);
        }

        m_waveFormat = (PWAVEFORMATEX) ExAllocatePoolWithTag(NonPagedPool, (pwfx->wFormatTag == WAVE_FORMAT_PCM) ? sizeof(PCMWAVEFORMAT) : sizeof(WAVEFORMATEX) + pwfx->cbSize, MSVAD_POOLTAG);
        if(m_waveFormat) {
            RtlCopyMemory(m_waveFormat, pwfx,(pwfx->wFormatTag == WAVE_FORMAT_PCM) ? sizeof(PCMWAVEFORMAT) : sizeof(WAVEFORMATEX) + pwfx->cbSize);
        } else {
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    return ntStatus;
} // SetDataFormat

//=============================================================================
#pragma code_seg()
void CSaveData::SaveFrame(IN ULONG ulFrameNo, IN ULONG ulDataSize) {
    PSAVEWORKER_PARAM           pParam = NULL;

    DPF_ENTER(("[CSaveData::SaveFrame]"));

    pParam = GetNewWorkItem();
    if (pParam) {
        pParam->pSaveData = this;
        pParam->ulFrameNo = ulFrameNo;
        pParam->ulOffset = ulFrameNo * m_ulFrameSize;
        pParam->ulDataSize = ulDataSize;
        KeResetEvent(&pParam->EventDone);
        IoQueueWorkItem(pParam->WorkItem, SaveFrameWorkerCallback, CriticalWorkQueue, (PVOID)pParam);
    }
} // SaveFrame

#pragma code_seg("PAGE")
//=============================================================================
void CSaveData::WaitAllWorkItems(void) {
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::WaitAllWorkItems]"));

    // Save the last partially-filled frame
    SaveFrame(m_ulFramePtr, m_ulBufferPtr - (m_ulFramePtr * m_ulFrameSize));

    for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++) {
        DPF(D_VERBOSE, ("[Waiting for WorkItem] %d", i));
        KeWaitForSingleObject(&(m_pWorkItems[i].EventDone), Executive, KernelMode, FALSE, NULL);
    }
} // WaitAllWorkItems

#pragma code_seg()
//=============================================================================
void CSaveData::WriteData(IN PBYTE pBuffer, IN ULONG ulByteCount) {
    ASSERT(pBuffer);

    BOOL  fSaveFrame = FALSE;
    ULONG ulSaveFramePtr = 0;

    // If stream writing is disabled, then exit.
    if (m_fWriteDisabled) {
        return;
    }

    DPF_ENTER(("[CSaveData::WriteData ulByteCount=%lu]", ulByteCount));

    if( 0 == ulByteCount ) {
        return;
    }

    // Check to see if this frame is available.
    KeAcquireSpinLockAtDpcLevel( &m_FrameInUseSpinLock );
    if (!m_fFrameUsed[m_ulFramePtr]) {
        KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );

        ULONG ulWriteBytes = ulByteCount;

        if((m_ulBufferSize - m_ulBufferPtr) < ulWriteBytes) {
            ulWriteBytes = m_ulBufferSize - m_ulBufferPtr;
        }

        RtlCopyMemory(m_pDataBuffer + m_ulBufferPtr, pBuffer, ulWriteBytes);
        m_ulBufferPtr += ulWriteBytes;

        // Check to see if we need to save this frame
        if (m_ulBufferPtr >= ((m_ulFramePtr + 1) * m_ulFrameSize)) {
            fSaveFrame = TRUE;
        }

        // Loop the buffer, if we reached the end.
        if (m_ulBufferPtr == m_ulBufferSize) {
            fSaveFrame = TRUE;
            m_ulBufferPtr = 0;
        }

        if (fSaveFrame) {
            InterlockedExchange((LONG *)&(m_fFrameUsed[m_ulFramePtr]), TRUE);
            ulSaveFramePtr = m_ulFramePtr;
            m_ulFramePtr = (m_ulFramePtr + 1) % m_ulFrameCount;
        }

        // Write the left over if the next frame is available.
        if (ulWriteBytes != ulByteCount) {
            KeAcquireSpinLockAtDpcLevel( &m_FrameInUseSpinLock );
            if (!m_fFrameUsed[m_ulFramePtr]) {
                KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );
                RtlCopyMemory(m_pDataBuffer + m_ulBufferPtr, pBuffer + ulWriteBytes, ulByteCount - ulWriteBytes);
                m_ulBufferPtr += ulByteCount - ulWriteBytes;
            } else {
                KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );
                DPF(D_BLAB, ("[Frame overflow, next frame is in use]"));
            }
        }

        if (fSaveFrame) {
            SaveFrame(ulSaveFramePtr, m_ulFrameSize);
        }
    } else {
        KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );
        DPF(D_BLAB, ("[Frame %d is in use]", m_ulFramePtr));
    }

} // WriteData


