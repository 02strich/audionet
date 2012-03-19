#include <stdlib.h>

#include <windows.h>
#include <devioctl.h>
#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>

#include "resource.h"
#include "driver/prvprop.h"

//=============================================================================
// Types
//=============================================================================

//=============================================================================
// Forward Definitions
//=============================================================================
INT_PTR APIENTRY AudioNetDlgProc(HWND hDlg, UINT uMessage, WPARAM wParam, LPARAM lParam);
BOOL AudioNetPropPage_OnInitDialog(HWND ParentHwnd, HWND FocusHwnd, LPARAM lParam);
BOOL AudioNetPropPage_OnApplyDialog(HWND ParentHwnd);
BOOL AudioNetPropPage_OnExitDialog(void);
void SetAudioNetServer(PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData, PAudioNetServer pAudioNetServer);
BOOL GetAudioNetServer(PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData, PAudioNetServer pAudioNetServer);
BOOL GetDeviceInterfaceDetail(PSP_PROPSHEETPAGE_REQUEST pPropPageRequest, PSP_DEVICE_INTERFACE_DETAIL_DATA *ppDeviceInterfaceDetailData);

//=============================================================================
// Global Variables
//=============================================================================
HINSTANCE                        ghInstance;
PSP_DEVICE_INTERFACE_DETAIL_DATA gpDeviceInterfaceDetailData;


//=============================================================================
void dbgError(LPCTSTR szMsg, bool showLastError = true)
/*++
 Routine Description:
    This function prints an error message.
    It prints first the string passed and then the error that it gets with
    GetLastError as a string.

 Arguments:
    szMsg - message to print.

 Return Value:
    None.
--*/
{
    LPTSTR errorMessage = 0;
    DWORD  count        = 0;

    // Get the error message from the system if needed
    if(showLastError)
        count = FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            GetLastError (),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&errorMessage,
            0,
            NULL);

    // Print the msg + error + \n\r.
    if (count || !showLastError) {
        OutputDebugString(szMsg);
        if(showLastError)
            OutputDebugString(errorMessage);
        OutputDebugString(TEXT("\n\r"));

        // This is for those without a debugger.
        // MessageBox (NULL, szErrorMessage, szMsg, MB_OK | MB_ICONSTOP);

        LocalFree (errorMessage);
    } else {
        OutputDebugString(TEXT("AudioNet Property Page: Low memory condition. Cannot print error message.\n\r"));
    }
}

//=============================================================================
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
/*++
 Routine Description:
    Main enty point of the DLL.
    We safe only the instance handle that we need for the creation of the
    property sheet. There is nothing else to do.

 Arguments:
    hModule            - instance data, is equal to module handle
    ul_reason_for_call - the reason for the call
    lpReserved         - some additional parameter.

 Return Value:
    BOOL: FALSE if DLL should fail, TRUE on success
--*/
{
    UNREFERENCED_PARAMETER(ul_reason_for_call);
    UNREFERENCED_PARAMETER(lpReserved);

    ghInstance = (HINSTANCE) hModule;
    return TRUE;
}

//=============================================================================
BOOL APIENTRY AudioNetPropPageProvider(PSP_PROPSHEETPAGE_REQUEST pPropPageRequest, LPFNADDPROPSHEETPAGE fAddFunc, LPARAM lParam)
/*++
 Routine Description:
    This function gets called by the device manager when it asks for additional
    property sheet pages. The parameter fAddFunc is the function that we call to
    add the sheet page to the dialog.
    This routine gets called because the registry entry "EnumPropPage32" tells
    the device manager that there is a dll with a exported function that will add
    a property sheet page.
    Because we want to fail this function (not create the sheet) if the driver
    doesn't support the private property, we have to do all the work here, that
    means we open the device and get all the information, then we close the
    device and return.

 Arguments:
    pPropPageRequest - points to SP_PROPSHEETPAGE_REQUEST
    fAddFunc         - function ptr to call to add sheet.
    lparam           - add sheet functions private data handle.

 Return Value:
    BOOL: FALSE if pages could not be added, TRUE on success
--*/
{
    PAudioNetServer                  pAudioNetServer;
    PROPSHEETPAGE                    PropSheetPage;
    HPROPSHEETPAGE                   hPropSheetPage;

    // Check page requested
    if (pPropPageRequest->PageRequested != SPPSR_ENUM_ADV_DEVICE_PROPERTIES) {
        return FALSE;
    }

    // Check device info set and data
    if ((!pPropPageRequest->DeviceInfoSet) || (!pPropPageRequest->DeviceInfoData)) {
        return FALSE;
    }

    // Get the device interface detail which return a path to the device
    // driver that we need to open the device.
    if (!GetDeviceInterfaceDetail(pPropPageRequest, &gpDeviceInterfaceDetailData)) {
        return FALSE;
    }
    
    // Allocate the memory for the AudioNet server property.
    pAudioNetServer = (PAudioNetServer)LocalAlloc(LPTR, sizeof(AudioNetServer));
    if (!pAudioNetServer) {
        dbgError(TEXT("[AudioNetPropPageProvider] LocalAlloc: "));
        LocalFree(gpDeviceInterfaceDetailData);
        return FALSE;
    }

    // Get the AudioNet server name through the private property call.
    if (!GetAudioNetServer(gpDeviceInterfaceDetailData, pAudioNetServer)) {
        LocalFree(gpDeviceInterfaceDetailData);
        LocalFree(pAudioNetServer);
        return FALSE;
    }

    // initialize the property page
    PropSheetPage.dwSize        = sizeof(PROPSHEETPAGE);
    PropSheetPage.dwFlags       = 0;
    PropSheetPage.hInstance     = ghInstance;
    PropSheetPage.pszTemplate   = MAKEINTRESOURCE(DLG_AUDIONETSERVER);
    PropSheetPage.pfnDlgProc    = AudioNetDlgProc;
    PropSheetPage.lParam        = (LPARAM)pAudioNetServer;
    PropSheetPage.pfnCallback   = NULL;

    // create the page and get back a handle
    hPropSheetPage = CreatePropertySheetPage(&PropSheetPage);
    if (!hPropSheetPage) {
        LocalFree(gpDeviceInterfaceDetailData);
        LocalFree(pAudioNetServer);
        return FALSE;
    }

    // add the property page
    if (!(*fAddFunc)(hPropSheetPage, lParam)) {
        DestroyPropertySheetPage(hPropSheetPage);
        LocalFree(gpDeviceInterfaceDetailData);
        LocalFree(pAudioNetServer);
        return FALSE;
    }

    return TRUE;
}

//=============================================================================
INT_PTR APIENTRY AudioNetDlgProc(HWND hDlg, UINT uMessage, WPARAM wParam, LPARAM lParam)
/*++
 Routine Description:
    This callback function gets called by the system whenever something happens
    with the dialog sheet. Please take a look at the SDK for further information
    on dialog messages.

 Arguments:
    hDlg     - handle to the dialog window
    uMessage - the message
    wParam   - depending on message sent
    lParam   - depending on message sent

 Return Value:
    int (depends on message).
--*/
{
    switch (uMessage) {
        // We don't do anything for these messages.
        case WM_COMMAND:
        case WM_CONTEXTMENU:
        case WM_HELP:
            break;
        
        case WM_DESTROY:
            return AudioNetPropPage_OnExitDialog();
            break;
        
        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code) {
                case PSN_APPLY:
                    return AudioNetPropPage_OnApplyDialog(hDlg);
                    break;
            }
            break;

        case WM_INITDIALOG:
            return AudioNetPropPage_OnInitDialog(hDlg, (HWND) wParam, lParam);
    }

    return FALSE;
}

//=============================================================================
BOOL AudioNetPropPage_OnInitDialog(HWND ParentHwnd, HWND FocusHwnd, LPARAM lParam)
/*++
 Routine Description:
    This function gets called when the property sheet page gets created.  This
    is the perfect opportunity to initialize the dialog items that get displayed.

 Arguments:
    ParentHwnd - handle to the dialog window
    FocusHwnd  - handle to the control that would get the focus.
    lParam     - initialization parameter (pAC97Features).

 Return Value:
    TRUE if focus should be set to FocusHwnd, FALSE if you set the focus yourself.
--*/
{
    PAudioNetServer   pAudioNetServer;
    HCURSOR           hCursor;
    wchar_t           wServerName[255];

    UNREFERENCED_PARAMETER(FocusHwnd);

    dbgError(TEXT("[AudioNetPropPage_OnInitDialog] "), false);
    
    // Check the parameters (lParam is tAC97Features pointer)
    if (!lParam)
        return FALSE;
    
    // put up the wait cursor
    hCursor = SetCursor(LoadCursor (NULL, IDC_WAIT));

    // get current value
    pAudioNetServer = (PAudioNetServer)((LPPROPSHEETPAGE)lParam)->lParam;
    
    // convert ASCII servername to unicode
    mbstowcs(wServerName, pAudioNetServer->name, 255);
    
    // set the server name
    SetWindowText(GetDlgItem(ParentHwnd, IDC_SERVERNAME), wServerName);

    // We don't need the private structure anymore.
    LocalFree(pAudioNetServer);

    // remove the wait cursor
    SetCursor(hCursor);

    return TRUE;
}

//=============================================================================
BOOL AudioNetPropPage_OnApplyDialog(HWND ParentHwnd)
/*++
 Routine Description:
    This function gets called when the property sheet page gets created.  This
    is the perfect opportunity to initialize the dialog items that get displayed.

 Arguments:
    ParentHwnd - handle to the dialog window

 Return Value:
    TRUE if focus should be set to FocusHwnd, FALSE if you set the focus yourself.
--*/
{
    PAudioNetServer                  pAudioNetServer;
    wchar_t                          wServerName[255];
    
    dbgError(TEXT("[AudioNetPropPage_OnApplyDialog]"), false);
    
    // get current value of text field
    GetWindowText(GetDlgItem(ParentHwnd, IDC_SERVERNAME), (LPWSTR)&wServerName, 255);
    
    // Allocate the memory for the AudioNet server property.
    pAudioNetServer = (PAudioNetServer)LocalAlloc(LPTR, sizeof(AudioNetServer));
    if (!pAudioNetServer) {
        dbgError(TEXT("[AudioNetPropPage_OnApplyDialog] LocalAlloc: "));
        return FALSE;
    }
    
    // copy value to AudioNetServer structure
    wcstombs(pAudioNetServer->name, (const wchar_t *)&wServerName, 255);

    // Set the AudioNet server name through the private property call.
    SetAudioNetServer(gpDeviceInterfaceDetailData, pAudioNetServer);

    // clean-up
    LocalFree(gpDeviceInterfaceDetailData), gpDeviceInterfaceDetailData = NULL;
    LocalFree(pAudioNetServer);
    
    return TRUE;
}

//=============================================================================
BOOL AudioNetPropPage_OnExitDialog(void) {
    dbgError(TEXT("[AudioNetPropPage_OnExitDialog]"), false);
    
    if (gpDeviceInterfaceDetailData != NULL) {
        LocalFree(gpDeviceInterfaceDetailData);
    }
    
    return TRUE;
}

//=============================================================================
void SetAudioNetServer(PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData, PAudioNetServer pAudioNetServer)
/*++
 Routine Description:
    This function gets called by the property page provider (in this module) to
    show how we could set properties in the driver.  Note that this is only an
    example and doesn't really do anything useful.
    We pass down a DWORD and the driver will simply print this DWORD out on the
    debugger.

 Arguments:
    pDeviceInterfaceDetailData - device interface details (path to device driver)

 Return Value:
    None
--*/
{
    HANDLE          hTopology;
    KSPROPERTY      AudioNetServerProperty;
    ULONG           ulBytesReturned;
    BOOL            fSuccess;

    // Open the topology filter.
    hTopology = CreateFile(pDeviceInterfaceDetailData->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hTopology == INVALID_HANDLE_VALUE) {
        dbgError(TEXT("[SetAudioNetServer] CreateFile: "));
        return;
    }
    
    // Prepare the property structure sent down.
    AudioNetServerProperty.Set = KSPROPSETID_Private;
    AudioNetServerProperty.Flags = KSPROPERTY_TYPE_SET;
    AudioNetServerProperty.Id = KSPROPERTY_STREAMING_ENDPOINT;

    // Make the final call.
    fSuccess = DeviceIoControl(hTopology, IOCTL_KS_PROPERTY, &AudioNetServerProperty, sizeof(AudioNetServerProperty), pAudioNetServer, sizeof(AudioNetServer), &ulBytesReturned, NULL);

    // We don't need the handle anymore.
    CloseHandle(hTopology);
    
    // Check for error.
    if (!fSuccess) {
        dbgError(TEXT("[SetAudioNetServer] DeviceIoControl: "));
    }

    return;
}
    
//=============================================================================
BOOL GetAudioNetServer(PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData, PAudioNetServer pAudioNetServer)
/*++
 Routine Description:
    This function gets called by the property page provider (in this module) to
    get the current AudioNet server that is normally not displayed by the drivers.
    To get the AudioNetServer structure we pass down the private property. As you
    can see this is fairly easy.

 Arguments:
    pDeviceInterfaceDetailData - device interface details (path to device driver)
    pAudioNetServer             - pointer to AudioNet server structure.

 Return Value:
    BOOL: FALSE if we couldn't get the information, TRUE on success. 
--*/
{
    HANDLE          hTopology;
    KSPROPERTY      AudioNetServerProperty;
    ULONG           ulBytesReturned;
    BOOL            fSuccess;

    // Open the topology filter.
    hTopology = CreateFile(pDeviceInterfaceDetailData->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hTopology == INVALID_HANDLE_VALUE) {
        dbgError(TEXT("[GetAudioNetServer] CreateFile: "));
        return FALSE;
    }

    // Fill the KSPROPERTY structure.
    AudioNetServerProperty.Set = KSPROPSETID_Private;
    AudioNetServerProperty.Flags = KSPROPERTY_TYPE_GET;
    AudioNetServerProperty.Id = KSPROPERTY_STREAMING_ENDPOINT;

    // ask the device
    fSuccess = DeviceIoControl(hTopology, IOCTL_KS_PROPERTY, &AudioNetServerProperty, sizeof(AudioNetServerProperty), pAudioNetServer, sizeof(AudioNetServer), &ulBytesReturned, NULL);

    // We don't need the handle anymore.
    CloseHandle(hTopology);
    
    // Check for error.
    if (!fSuccess) {
        dbgError(TEXT("[GetAudioNetServer] DeviceIoControl: "));
        return FALSE;
    }

    return TRUE;
}

//=============================================================================
BOOL GetDeviceInterfaceDetail(PSP_PROPSHEETPAGE_REQUEST pPropPageRequest, PSP_DEVICE_INTERFACE_DETAIL_DATA *ppDeviceInterfaceDetailData)
/*++
 Routine Description:
    This function gets called by the property page provider (in this module) to
    get the device interface details. The device interface detail contains a
    path to the device driver that can be used to open the device driver.
    When we parse the driver we look for the topology interface since this
    interface exposes the private property.

 Arguments:
    pPropPageRequest           - points to SP_PROPSHEETPAGE_REQUEST
    pDeviceInterfaceDetailData - device interface details returned.

 Return Value:
    BOOL: FALSE if something went wrong, TRUE on success.
--*/
{
    BOOL                        fSuccess;
    ULONG                       ulDeviceInstanceIdSize = 0;
    PTSTR                       pDeviceInstanceID = NULL;
    HDEVINFO                    hDevInfoWithInterface;
    SP_DEVICE_INTERFACE_DATA    DeviceInterfaceData;
    ULONG                       ulDeviceInterfaceDetailDataSize = 0;

    // Get the device instance id (PnP string).  The first call will retrieve
    // the buffer length in characters.  fSuccess will be FALSE.
    fSuccess = SetupDiGetDeviceInstanceId(pPropPageRequest->DeviceInfoSet, pPropPageRequest->DeviceInfoData, NULL, 0, &ulDeviceInstanceIdSize);

    // Check for error.
    if ((GetLastError () != ERROR_INSUFFICIENT_BUFFER) || (!ulDeviceInstanceIdSize)) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] SetupDiGetDeviceInstanceId: "));
        return FALSE;
    }

    // Allocate the buffer for the device instance ID (PnP string).
    pDeviceInstanceID = (PTSTR)LocalAlloc(LPTR, ulDeviceInstanceIdSize * sizeof (TCHAR));
    if (!pDeviceInstanceID) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] LocalAlloc: "));
        return FALSE;
    }
    
    // Now call again, this time with all parameters.
    fSuccess = SetupDiGetDeviceInstanceId(pPropPageRequest->DeviceInfoSet, pPropPageRequest->DeviceInfoData, pDeviceInstanceID, ulDeviceInstanceIdSize, NULL);
    if (!fSuccess) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] SetupDiGetDeviceInstanceId: "));
        LocalFree (pDeviceInstanceID);
        return FALSE;
    }

    // Now we can get the handle to the dev info with interface.
    // We parse the device specifically for topology interfaces.
    hDevInfoWithInterface = SetupDiGetClassDevs(&KSCATEGORY_TOPOLOGY, pDeviceInstanceID, NULL, DIGCF_DEVICEINTERFACE);

    // We don't need pDeviceInstanceID anymore.
    LocalFree (pDeviceInstanceID);

    // Check for error.
    if (hDevInfoWithInterface == INVALID_HANDLE_VALUE) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] SetupDiGetClassDevs: "));
        return FALSE;
    }

    // Go through the list of topology device interface of this device.
    // We assume that there is only one topology device interface and
    // we will store the device details in our private structure.
    DeviceInterfaceData.cbSize = sizeof(DeviceInterfaceData);
    fSuccess = SetupDiEnumDeviceInterfaces(hDevInfoWithInterface, NULL, &KSCATEGORY_TOPOLOGY, 0, &DeviceInterfaceData);
    if (!fSuccess) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] SetupDiEnumDeviceInterfaces: "));
        SetupDiDestroyDeviceInfoList(hDevInfoWithInterface);
        return FALSE;
    }

    // Get the details for this device interface.  The first call will retrieve
    // the buffer length in characters.  fSuccess will be FALSE.
    fSuccess = SetupDiGetDeviceInterfaceDetail(hDevInfoWithInterface, &DeviceInterfaceData, NULL, 0, &ulDeviceInterfaceDetailDataSize, NULL);
    if ((GetLastError () != ERROR_INSUFFICIENT_BUFFER) || (!ulDeviceInterfaceDetailDataSize)) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] SetupDiGetDeviceInterfaceDetail: "));
        SetupDiDestroyDeviceInfoList(hDevInfoWithInterface);
        return FALSE;
    }

    // Allocate the buffer for the device interface detail data.
    *ppDeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) LocalAlloc(LPTR, ulDeviceInterfaceDetailDataSize);
    if (!*ppDeviceInterfaceDetailData) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] LocalAlloc: "));
        SetupDiDestroyDeviceInfoList (hDevInfoWithInterface);
        return FALSE;
    }
    
    // The size contains only the structure, not the additional path.
    (*ppDeviceInterfaceDetailData)->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    // Get the details for this device interface, this time with all paramters.
    fSuccess = SetupDiGetDeviceInterfaceDetail(hDevInfoWithInterface, &DeviceInterfaceData, *ppDeviceInterfaceDetailData, ulDeviceInterfaceDetailDataSize, NULL, NULL);

    // We don't need the handle anymore.
    SetupDiDestroyDeviceInfoList (hDevInfoWithInterface);

    if (!fSuccess) {
        dbgError(TEXT("[GetDeviceInterfaceDetail] SetupDiGetDeviceInterfaceDetail: "));
        LocalFree(*ppDeviceInterfaceDetailData), *ppDeviceInterfaceDetailData = NULL;
        return FALSE;
    }

    return TRUE;
}

