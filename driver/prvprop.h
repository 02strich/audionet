/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    prvprop.h

Abstract:
    Header file for private properties.
--*/

#ifndef _MSVAD_PRVPROP_H_
#define _MSVAD_PRVPROP_H_

//=============================================================================
// Defines
//=============================================================================

// This is the GUID for the private property request.
#define STATIC_KSPROPSETID_Private 0x2aa7a0b1L, 0x9f78, 0x4606, 0xb8, 0x82, 0x66, 0xb7, 0xf, 0x2, 0x26, 0x37
DEFINE_GUIDSTRUCT("2AA7A0B1-9F78-4606-B882-66B70F022637", KSPROPSETID_Private);
#define KSPROPSETID_Private DEFINE_GUIDNAMED(KSPROPSETID_Private)

//=============================================================================
// Constants
//=============================================================================
const int KSPROPERTY_STREAMING_ENDPOINT = 1;

//=============================================================================
// Types
//=============================================================================
typedef struct {
	wchar_t name[255];
} AudioNetServer;
typedef AudioNetServer *PAudioNetServer;

#endif