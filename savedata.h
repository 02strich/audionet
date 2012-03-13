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
    ULONG            ulDataSize;
    PBYTE            pData;
    PCSaveData       pSaveData;
    KEVENT           EventDone;
} SAVEWORKER_PARAM;
typedef SAVEWORKER_PARAM *PSAVEWORKER_PARAM;
#include <poppack.h>

// wave file header.
#include <pshpack1.h>
typedef struct _OUTPUT_FILE_HEADER {
    DWORD           dwRiff;
    DWORD           dwFileSize;
    DWORD           dwWave;
    DWORD           dwFormat;
    DWORD           dwFormatLength;
} OUTPUT_FILE_HEADER;
typedef OUTPUT_FILE_HEADER *POUTPUT_FILE_HEADER;

typedef struct _OUTPUT_DATA_HEADER {
    DWORD           dwData;
    DWORD           dwDataLength;
} OUTPUT_DATA_HEADER;
typedef OUTPUT_DATA_HEADER *POUTPUT_DATA_HEADER;

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
	
	// Data buffer and MDL used by send operations
    PVOID						m_dataBuffer;
    PMDL						m_dataMdl;
    ULONG						m_bufferLength;
	ULONG						m_dataLength;
	
	PWAVEFORMATEX               m_waveFormat;
	
    static PDEVICE_OBJECT       m_pDeviceObject;
    static ULONG                m_ulStreamId;

    BOOL                        m_fWriteDisabled;

    BOOL                        m_bInitialized;

public:
    CSaveData();
    ~CSaveData();

	NTSTATUS                    Initialize(void);
	NTSTATUS                    SetDataFormat(IN  PKSDATAFORMAT pDataFormat);
	void                        Disable(BOOL fDisable);
		
	static NTSTATUS             SetDeviceObject(IN PDEVICE_OBJECT DeviceObject);
	static PDEVICE_OBJECT       GetDeviceObject(void);
    
    void                        WriteData(IN PBYTE pBuffer, IN ULONG ulByteCount);
};
typedef CSaveData *PCSaveData;

#endif
