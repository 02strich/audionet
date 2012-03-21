/*++

Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:

    minwave.h

Abstract:

    Definition of wavecyclic miniport class.

--*/

#ifndef _MSVAD_MINWAVE_H_
#define _MSVAD_MINWAVE_H_

//=============================================================================
// Referenced Forward
//=============================================================================
class CMiniportWaveCyclicStream;
typedef CMiniportWaveCyclicStream *PCMiniportWaveCyclicStream;

//=============================================================================
// Classes
//=============================================================================
///////////////////////////////////////////////////////////////////////////////
// CMiniportWaveCyclic 
//   

class CMiniportWaveCyclic : public IMiniportWaveCyclic, public CUnknown {
private:
    BOOL                        m_fCaptureAllocated;
    BOOL                        m_fRenderAllocated;

protected:
    PADAPTERCOMMON              m_AdapterCommon;    // Adapter common object
    PPORTWAVECYCLIC             m_Port;             // Callback interface
    PPCFILTER_DESCRIPTOR        m_FilterDescriptor; // Filter descriptor

    ULONG                       m_NotificationInterval; // milliseconds.
    ULONG                       m_SamplingFrequency;    // Frames per second.

    PSERVICEGROUP               m_ServiceGroup;     // For notification.
    KMUTEX                      m_SampleRateSync;   // Sync for sample rate 

    ULONG                       m_MaxDmaBufferSize; // Dma buffer size.

    // All the below members should be updated by the child classes
    ULONG                       m_MaxOutputStreams; // Max stream caps
    ULONG                       m_MaxInputStreams;
    ULONG                       m_MaxTotalStreams;

    ULONG                       m_MinChannels;      // Format caps
    ULONG                       m_MaxChannelsPcm;
    ULONG                       m_MinBitsPerSamplePcm;
    ULONG                       m_MaxBitsPerSamplePcm;
    ULONG                       m_MinSampleRatePcm;
    ULONG                       m_MaxSampleRatePcm;
	
	// Settings for new streams
	wchar_t						m_cServerName[255];

protected:
    NTSTATUS                    ValidateFormat(IN PKSDATAFORMAT pDataFormat);
    NTSTATUS                    ValidatePcm(IN PWAVEFORMATEX pWfx);

public:
    DECLARE_STD_UNKNOWN();
    CMiniportWaveCyclic(PUNKNOWN other);
    ~CMiniportWaveCyclic();

    IMP_IMiniportWaveCyclic;

    NTSTATUS                    PropertyHandlerComponentId(IN PPCPROPERTY_REQUEST PropertyRequest);
    NTSTATUS                    PropertyHandlerProposedFormat(IN PPCPROPERTY_REQUEST PropertyRequest);
    NTSTATUS                    PropertyHandlerCpuResources(IN PPCPROPERTY_REQUEST PropertyRequest);
    NTSTATUS                    PropertyHandlerGeneric(IN PPCPROPERTY_REQUEST PropertyRequest);
	NTSTATUS                    PropertyHandlerPrivate(IN PPCPROPERTY_REQUEST PropertyRequest);
	
	//STDMETHODIMP                GetDescription(OUT PPCFILTER_DESCRIPTOR *Description);
    //STDMETHODIMP                Init(IN PUNKNOWN UnknownAdapter, IN PRESOURCELIST ResourceList, IN PPORTWAVECYCLIC Port);

    // Friends
    friend class                CMiniportWaveCyclicStream;
    friend class                CMiniportTopology;
    friend void                 TimerNotify(IN PKDPC Dpc, IN PVOID DeferredContext, IN PVOID SA1, IN PVOID SA2);
};
typedef CMiniportWaveCyclic *PCMiniportWaveCyclic;

#endif

