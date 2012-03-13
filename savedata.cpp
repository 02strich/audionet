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

#include <msvad.h>
#include "savedata.h"
#include <ntstrsafe.h>   // This is for using RtlStringcbPrintf

//=============================================================================
// Defines
//=============================================================================
#define RIFF_TAG                    0x46464952;
#define WAVE_TAG                    0x45564157;
#define FMT__TAG                    0x20746D66;
#define DATA_TAG                    0x61746164;

#define DEFAULT_FRAME_COUNT         2
#define DEFAULT_FRAME_SIZE          PAGE_SIZE * 4
#define DEFAULT_BUFFER_SIZE         DEFAULT_FRAME_SIZE * DEFAULT_FRAME_COUNT

#define DEFAULT_FILE_NAME           L"\\DosDevices\\C:\\STREAM"

#define MAX_WORKER_ITEM_COUNT       15

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

SOCKADDR_IN IPv4LocalAddress = {
    AF_INET, 
    0x489c, // 40008 in hex in network byte order 
    { 0 },
    0};

SOCKADDR_IN IPv4RemoteAddress = {
    AF_INET, 
    0x5000, // 40009 in hex in network byte order 
    { 141, 89, 225, 120}, // send to 127.0.0.1
    0};


//=============================================================================
// Helper Functions
//=============================================================================
// IRP completion routine used for synchronously waiting for completion
NTSTATUS
WskSampleSyncIrpCompletionRoutine(
    __in PDEVICE_OBJECT Reserved,
    __in PIRP Irp,
    __in PVOID Context
    )
{    
    PKEVENT compEvent = (PKEVENT)Context;
    UNREFERENCED_PARAMETER(Reserved);
//    UNREFERENCED_PARAMETER(Irp);
    
    DPF(D_TERSE, ("IRP finished: %x", Irp->IoStatus.Status));
    
    KeSetEvent(compEvent, 2, FALSE);    
    return STATUS_MORE_PROCESSING_REQUIRED;
}

#pragma code_seg("PAGE")
//=============================================================================
// CSaveData
//=============================================================================

//=============================================================================
CSaveData::CSaveData() : m_socket(NULL), m_dataBuffer(NULL), m_dataMdl(NULL), m_bufferLength(2048), m_fWriteDisabled(FALSE), m_bInitialized(FALSE), m_dataLength(0) {
    PAGED_CODE();
    
    DPF_ENTER(("[CSaveData::CSaveData]"));
    
    NTSTATUS         ntStatus = STATUS_SUCCESS;
    WSK_CLIENT_NPI   wskClientNpi;
    
    // get us an IRP
    m_irp = IoAllocateIrp(1, FALSE);
    
    // initialize io completion sychronization event
    KeInitializeEvent(&m_syncEvent, SynchronizationEvent, FALSE);

    // Register with WSK.
    wskClientNpi.ClientContext = NULL;
    wskClientNpi.Dispatch = &WskSampleClientDispatch;
    ntStatus = WskRegister(&wskClientNpi, &m_wskSampleRegistration);

    m_ulStreamId++;
} // CSaveData

//=============================================================================
CSaveData::~CSaveData() {
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::~CSaveData]"));
    
    if(m_socket) {
        // close socket
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
    
    // clean-up internal buffer
    if (m_dataBuffer) {
        ExFreePoolWithTag(m_dataBuffer, MSVAD_POOLTAG);
    }
    
} // CSaveData

//=============================================================================
void CSaveData::Disable(BOOL fDisable) {
    PAGED_CODE();

    m_fWriteDisabled = fDisable;
} // Disable

//=============================================================================
NTSTATUS CSaveData::SetDeviceObject(
    IN  PDEVICE_OBJECT          DeviceObject
)
{
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

//=============================================================================
NTSTATUS CSaveData::Initialize(void) {
    PAGED_CODE();

    NTSTATUS         ntStatus = STATUS_SUCCESS;
    WSK_PROVIDER_NPI wskProviderNpi;

    DPF_ENTER(("[CSaveData::Initialize]"));
    
    // get us a buffer
    m_dataBuffer = ExAllocatePoolWithTag(NonPagedPool, m_bufferLength, MSVAD_POOLTAG);
    if(m_dataBuffer == NULL) {
        DPF(D_TERSE, ("Failed to allocate buffer"));
    }
    RtlZeroMemory(m_dataBuffer, m_bufferLength);
    
    // create MDL for buffer
    m_dataMdl = IoAllocateMdl(m_dataBuffer, m_bufferLength, FALSE, FALSE, NULL);
    if(m_dataMdl == NULL) {
        DPF(D_TERSE, ("Failed to allocate MDL"));
    }
    MmBuildMdlForNonPagedPool(m_dataMdl);
    
    // Capture the WSK Provider NPI
    ntStatus = WskCaptureProviderNPI(&m_wskSampleRegistration, WSK_NO_WAIT, &wskProviderNpi);
    
    if(NT_SUCCESS(ntStatus)) {
        // create datagram socket
        IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);        
        IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
        
        // We do not need to check the return status since the actual completion
        // status will be captured from the IRP after the IRP is completed.
        wskProviderNpi.Dispatch->WskSocket(
                wskProviderNpi.Client,
                AF_INET,
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
        
        if(NT_SUCCESS(m_irp->IoStatus.Status)) {
            DPF(D_TERSE, ("Successfully created socket"));
            
            // save created socket
            m_socket = (PWSK_SOCKET)m_irp->IoStatus.Information;
        
            // Bind the socket to the wildcard address. Once bind is completed,
            // WSK provider will make WskAcceptEvent callbacks as connections arrive.
            IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
            IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
            ((PWSK_PROVIDER_CONNECTION_DISPATCH)m_socket->Dispatch)->WskBind(m_socket, (PSOCKADDR)&IPv4LocalAddress, 0, m_irp);
            KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
            
            if(!NT_SUCCESS(m_irp->IoStatus.Status)) {
                DPF(D_TERSE, ("Failed to bind socket: %x", m_irp->IoStatus.Status));
                DPF(D_TERSE, ("Failed to bind socket: %x", IPv4LocalAddress.sin_addr.S_un.S_addr));
            } else {
                DPF(D_TERSE, ("Successfully bound socket"));
            }
        } else {
            DPF(D_TERSE, ("Failed to create socket: %x", m_irp->IoStatus.Status));
            if(m_irp->IoStatus.Information) {
                IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
                IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
                ((PWSK_PROVIDER_BASIC_DISPATCH)((PWSK_SOCKET)m_irp->IoStatus.Information)->Dispatch)->WskCloseSocket((PWSK_SOCKET)m_irp->IoStatus.Information, m_irp);
                KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
            }
        }
        ntStatus = m_irp->IoStatus.Status;
        
        // Release the WSK provider NPI since we won't use it anymore
        WskReleaseProviderNPI(&m_wskSampleRegistration);
    }

    return ntStatus;
} // Initialize

//=============================================================================
NTSTATUS CSaveData::SetDataFormat(
    IN PKSDATAFORMAT            pDataFormat
)
{
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

#pragma code_seg()
//=============================================================================
void CSaveData::WriteData(
    IN  PBYTE                   pBuffer,
    IN  ULONG                   ulByteCount
)
{
    ASSERT(pBuffer);

    WSK_BUF     wskbuf;
    char        sendstr[] = "testing\n";

    // If stream writing is disabled, then exit.
    if (m_fWriteDisabled) {
        return;
    }
    
    if( m_dataLength != 0) {
        return;
    }

    DPF_ENTER(("[CSaveData::WriteData ulByteCount=%lu]", ulByteCount));

    if( 0 == ulByteCount ) {
        return;
    }
    
    //wskbuf.Mdl = m_dataMdl;
    //wskbuf.Offset = 0;
    //wskbuf.Length = 16;
    wskbuf.Mdl = IoAllocateMdl(sendstr, sizeof(sendstr), FALSE, FALSE, NULL);
    MmBuildMdlForNonPagedPool(wskbuf.Mdl);
    wskbuf.Offset = 0;
	wskbuf.Length = sizeof(sendstr);

    IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
    IoSetCompletionRoutine(m_irp, WskSampleSyncIrpCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);

    //((PWSK_PROVIDER_DATAGRAM_DISPATCH)m_socket->Dispatch)->WskReceiveFrom(m_socket, &wskbuf, 0, NULL, 0, NULL, NULL, m_irp);
    ((PWSK_PROVIDER_DATAGRAM_DISPATCH)m_socket->Dispatch)->WskSendTo(m_socket, &wskbuf, 0, (PSOCKADDR)&IPv4RemoteAddress, 0, NULL, m_irp);
    KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);

    m_dataLength = 1;
    DPF(D_TERSE, ("Failed to write data: %x", m_irp->IoStatus.Status));
} // WriteData


