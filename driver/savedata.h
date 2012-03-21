/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    savedata.h

Abstract:
    Declaration of MSVAD data saving class. This class supplies services to save data to disk.
--*/

#ifndef _MSVAD_SAVEDATA_H
#define _MSVAD_SAVEDATA_H

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/union
#pragma warning(disable:4214) // bit field types other than int

// fix strange warnings from wsk.h
#pragma warning(disable:4510)
#pragma warning(disable:4512)
#pragma warning(disable:4610)

#include <ntddk.h>
#include <wsk.h>

#pragma warning(pop)

//-----------------------------------------------------------------------------
//  Forward declaration
//-----------------------------------------------------------------------------
class CSaveData;
typedef CSaveData *PCSaveData;

//-----------------------------------------------------------------------------
//  Structs
//-----------------------------------------------------------------------------

// Parameter to workitem.
#include <pshpack1.h>
typedef struct _SAVEWORKER_PARAM {
    PIO_WORKITEM     WorkItem;
    ULONG            ulFrameNo;
	ULONG			 ulOffset;
    ULONG            ulDataSize;
	ULONG			 padding;
    PCSaveData       pSaveData;
    KEVENT           EventDone;
} SAVEWORKER_PARAM;
typedef SAVEWORKER_PARAM *PSAVEWORKER_PARAM;
#include <poppack.h>

//-----------------------------------------------------------------------------
//  Classes
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
// CSaveData
//   Saves the wave data to disk.
//
IO_WORKITEM_ROUTINE SaveFrameWorkerCallback;

class CSaveData {
protected:
	WSK_REGISTRATION			m_wskSampleRegistration;
	PWSK_SOCKET					m_socket;
	PIRP						m_irp;
	KEVENT						m_syncEvent;
	
    PBYTE                       m_pDataBuffer;      // Data buffer.
    ULONG                       m_ulBufferSize;     // Total buffer size.
	PMDL						m_pDataMdl;			// MDL describing the buffer

    ULONG                       m_ulFramePtr;       // Current Frame.
    ULONG                       m_ulFrameCount;     // Frame count.
    ULONG                       m_ulFrameSize;
    ULONG                       m_ulBufferPtr;      // Pointer in buffer.
    PBOOL                       m_fFrameUsed;       // Frame usage table.
    KSPIN_LOCK                  m_FrameInUseSpinLock; // Spinlock for synch.

    PWAVEFORMATEX               m_waveFormat;

    static PDEVICE_OBJECT       m_pDeviceObject;
    static ULONG                m_ulStreamId;
    static PSAVEWORKER_PARAM    m_pWorkItems;

    BOOL                        m_fWriteDisabled;

    BOOL                        m_bInitialized;
	
	wchar_t						m_cServerName[255];
	SOCKADDR_STORAGE       		m_sServerAddr;

public:
    CSaveData();
    ~CSaveData();

	NTSTATUS                    Initialize(const wchar_t* cServerName);
	NTSTATUS                    SetDataFormat(IN  PKSDATAFORMAT pDataFormat);
	void                        Disable(BOOL fDisable);
	
    static void                 DestroyWorkItems(void);
    static PSAVEWORKER_PARAM    GetNewWorkItem(void);
    void                        WaitAllWorkItems(void);
	
	static NTSTATUS             SetDeviceObject(IN PDEVICE_OBJECT DeviceObject);
	static PDEVICE_OBJECT       GetDeviceObject(void);
    
    void                        WriteData(IN PBYTE pBuffer, IN ULONG ulByteCount);

private:
    static NTSTATUS             InitializeWorkItems(IN PDEVICE_OBJECT DeviceObject);

	void                        CreateSocket(void);
	void						SendData(ULONG offset, ULONG length);
	
    void                        SaveFrame(IN ULONG ulFrameNo, IN ULONG ulDataSize);
    friend VOID                 SaveFrameWorkerCallback(PDEVICE_OBJECT pDeviceObject, IN PVOID Context);
};
typedef CSaveData *PCSaveData;

#endif
