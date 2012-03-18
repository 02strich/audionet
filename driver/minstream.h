/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    minstream.h

Abstract:
    Definition of wavecyclic miniport class.
--*/

#ifndef _MSVAD_MINSTREAM_H_
#define _MSVAD_MINSTREAM_H_

#include "savedata.h"

//=============================================================================
// Referenced Forward
//=============================================================================
class CMiniportWaveCyclic;
typedef CMiniportWaveCyclic* PCMiniportWaveCyclic;

//=============================================================================
// Classes
//=============================================================================
///////////////////////////////////////////////////////////////////////////////
// CMiniportWaveCyclicStream 
// It implements basic functionality for wavecyclic streams.

class CMiniportWaveCyclicStream : public IMiniportWaveCyclicStream, public IDmaChannel, public CUnknown {
protected:
	PCMiniportWaveCyclic        m_pMiniport;                        // Miniport that created us
    BOOLEAN                     m_fCapture;                         // Capture or render.
    BOOLEAN                     m_fFormat16Bit;                     // 16- or 8-bit samples.
    USHORT                      m_usBlockAlign;                     // Block alignment of current format.
    KSSTATE                     m_ksState;                          // Stop, pause, run.
    ULONG                       m_ulPin;                            // Pin Id.

    PRKDPC                      m_pDpc;                             // Deferred procedure call object
    PKTIMER                     m_pTimer;                           // Timer object

    BOOLEAN                     m_fDmaActive;                       // Dma currently active? 
    ULONG                       m_ulDmaPosition;                    // Position in Dma
    PVOID                       m_pvDmaBuffer;                      // Dma buffer pointer
    ULONG                       m_ulDmaBufferSize;                  // Size of dma buffer
    ULONG                       m_ulDmaMovementRate;                // Rate of transfer specific to system
    ULONGLONG                   m_ullDmaTimeStamp;                  // Dma time elasped 
    ULONGLONG                   m_ullElapsedTimeCarryForward;       // Time to carry forward in position calc.
    ULONG                       m_ulByteDisplacementCarryForward;   // Bytes to carry forward to next calc.

    CSaveData                   m_SaveData;                         // Object to save settings.

public:
    DECLARE_STD_UNKNOWN();
    CMiniportWaveCyclicStream(PUNKNOWN other);
    ~CMiniportWaveCyclicStream();

    IMP_IMiniportWaveCyclicStream;
    IMP_IDmaChannel;

    NTSTATUS Init(IN PCMiniportWaveCyclic Miniport, IN ULONG Channel, IN  BOOLEAN Capture, IN PKSDATAFORMAT DataFormat);

    // Friends
    friend class                CMiniportWaveCyclic;
};
typedef CMiniportWaveCyclicStream *PCMiniportWaveCyclicStream;

#endif