#include <tchar.h>
#include <initguid.h>
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <devpkey.h>

#include "hid.h"

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

static BOOL got_hid_class = FALSE;
static GUID hid_class;

GUID hid_get_class()
{
    if (!got_hid_class)
    {
        HidD_GetHidGuid(&hid_class);
        got_hid_class = TRUE;
    }
    return hid_class;
}

struct hid_device_info *hid_enumerate(LPTSTR path_filter)
{
    struct hid_device_info *root_dev = NULL;
    struct hid_device_info *cur_dev = NULL;

    GUID class_guid = hid_get_class();
    SP_DEVINFO_DATA devinfo_data;
    SP_DEVICE_INTERFACE_DATA device_interface_data;
    SP_DEVICE_INTERFACE_DETAIL_DATA *device_interface_detail_data = NULL;
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    DWORD required_size = 0;
    DEVPROPTYPE prop_type;
    LPTSTR desc_buffer = NULL;
    LPWSTR desc_buffer_w = NULL;

    memset(&devinfo_data, 0x0, sizeof(devinfo_data));
    devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
    device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    device_info_set = SetupDiGetClassDevs(&class_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        return NULL;
    }

    DWORD device_index = 0;
    while (SetupDiEnumDeviceInfo(device_info_set, device_index, &devinfo_data))
    {
        DWORD device_interface_index = 0;
        while (SetupDiEnumDeviceInterfaces(device_info_set, &devinfo_data, &class_guid, device_interface_index, &device_interface_data))
        {
            SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, NULL, 0, &required_size, NULL);
            device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA *)malloc(required_size);
            device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, device_interface_detail_data, required_size, NULL, NULL))
            {
                if (path_filter == NULL || _tcsstr(device_interface_detail_data->DevicePath, path_filter) != NULL)
                {
                    desc_buffer = NULL;

                    if (SetupDiGetDevicePropertyW(device_info_set, &devinfo_data, &DEVPKEY_Device_BusReportedDeviceDesc,
                                                  &prop_type, NULL, 0, &required_size, 0))
                    {
                        desc_buffer_w = (LPWSTR)malloc(required_size);
                        memset(desc_buffer_w, 0, required_size);
                        SetupDiGetDevicePropertyW(device_info_set, &devinfo_data, &DEVPKEY_Device_BusReportedDeviceDesc,
                                                  &prop_type, (PBYTE)desc_buffer_w, required_size, NULL, 0);
#ifdef UNICODE
                        desc_buffer = desc_buffer_w;
#else
                        int desc_buffer_size = WideCharToMultiByte(CP_ACP, 0, desc_buffer_w, -1, desc_buffer, 0, NULL, NULL);
                        desc_buffer = (LPSTR)malloc(desc_buffer_size);
                        WideCharToMultiByte(CP_ACP, 0, desc_buffer_w, -1, desc_buffer, desc_buffer_size, NULL, NULL);
                        free(desc_buffer_w);
#endif /* UNICODE */
                    }

                    if (desc_buffer == NULL || _tcslen(desc_buffer) == 0)
                    {
                        if (desc_buffer != NULL)
                        {
                            free(desc_buffer);
                        }
                        if (SetupDiGetDeviceRegistryProperty(device_info_set, &devinfo_data, SPDRP_DEVICEDESC,
                                                             NULL, NULL, 0, &required_size))
                        {
                            desc_buffer = (LPTSTR)malloc(required_size);
                            memset(desc_buffer, 0, required_size);
                            SetupDiGetDeviceRegistryProperty(device_info_set, &devinfo_data, SPDRP_DEVICEDESC,
                                                             NULL, desc_buffer, required_size, NULL);
                        }
                    }

                    struct hid_device_info *dev = (struct hid_device_info *)malloc(sizeof(struct hid_device_info));
                    dev->path = (LPTSTR)malloc(_tcslen(device_interface_detail_data->DevicePath) * sizeof(TCHAR));
                    _tcscpy(dev->path, device_interface_detail_data->DevicePath);
                    dev->description = desc_buffer;
                    dev->next = NULL;

                    if (root_dev == NULL)
                    {
                        root_dev = dev;
                    }
                    else
                    {
                        cur_dev->next = dev;
                    }
                    cur_dev = dev;
                }
            }

            free(device_interface_detail_data);

            device_interface_index++;
        }

        device_index++;
    }

    SetupDiDestroyDeviceInfoList(device_info_set);

    return root_dev;
}

BOOL hid_reenable_device(LPTSTR path)
{
    GUID class_guid = hid_get_class();
    SP_DEVINFO_DATA devinfo_data;
    SP_DEVICE_INTERFACE_DATA device_interface_data;
    SP_DEVICE_INTERFACE_DETAIL_DATA *device_interface_detail_data = NULL;
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    DWORD required_size = 0;

    memset(&devinfo_data, 0x0, sizeof(devinfo_data));
    devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
    device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    device_info_set = SetupDiGetClassDevs(&class_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    DWORD device_index = 0;
    while (SetupDiEnumDeviceInfo(device_info_set, device_index, &devinfo_data))
    {
        DWORD device_interface_index = 0;
        while (SetupDiEnumDeviceInterfaces(device_info_set, &devinfo_data, &class_guid, device_interface_index, &device_interface_data))
        {
            SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, NULL, 0, &required_size, NULL);
            device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA *)malloc(required_size);
            device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, device_interface_detail_data, required_size, NULL, NULL))
            {
                if (_tcscmp(device_interface_detail_data->DevicePath, path) == 0)
                {
                    SP_PROPCHANGE_PARAMS pc_params = {
                        .ClassInstallHeader = {
                            .cbSize = sizeof(SP_CLASSINSTALL_HEADER),
                            .InstallFunction = DIF_PROPERTYCHANGE},
                        .StateChange = DICS_DISABLE,
                        .Scope = DICS_FLAG_GLOBAL,
                        .HwProfile = 0};
                    BOOL res;
                    res = SetupDiSetClassInstallParams(device_info_set, &devinfo_data, (PSP_CLASSINSTALL_HEADER)&pc_params,
                                                       sizeof(SP_PROPCHANGE_PARAMS));
                    res = res && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, device_info_set, &devinfo_data);
                    pc_params.StateChange = DICS_ENABLE;
                    res = res && SetupDiSetClassInstallParams(device_info_set, &devinfo_data, (PSP_CLASSINSTALL_HEADER)&pc_params,
                                                              sizeof(SP_PROPCHANGE_PARAMS));
                    res = res && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, device_info_set, &devinfo_data);
                    free(device_interface_detail_data);
                    SetupDiDestroyDeviceInfoList(device_info_set);
                    return res;
                }
            }

            free(device_interface_detail_data);

            device_interface_index++;
        }

        device_index++;
    }

    SetupDiDestroyDeviceInfoList(device_info_set);

    return FALSE;
}

void hid_free_device_info(struct hid_device_info *device_info)
{
    free(device_info->description);
    free(device_info->path);
    free(device_info);
}

struct hid_device *hid_open_device(LPTSTR path, BOOL open_rw)
{
    DWORD desired_access = open_rw ? (GENERIC_WRITE | GENERIC_READ) : 0;
    DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    SECURITY_ATTRIBUTES security = {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle = TRUE};
    HANDLE handle = CreateFile(path, desired_access, share_mode, &security, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);

    if (handle == INVALID_HANDLE_VALUE)
    {
        return NULL;
    }

    PHIDP_PREPARSED_DATA pp_data = NULL;
    if (!HidD_GetPreparsedData(handle, &pp_data))
    {
        CloseHandle(handle);
        return NULL;
    }

    HIDP_CAPS caps;
    if (HidP_GetCaps(pp_data, &caps) != HIDP_STATUS_SUCCESS)
    {
        HidD_FreePreparsedData(pp_data);
        CloseHandle(handle);
        return NULL;
    }

    struct hid_device *dev = (struct hid_device *)malloc(sizeof(struct hid_device));
    dev->path = (LPTSTR)malloc(_tcslen(path) * sizeof(TCHAR));
    _tcscpy(dev->path, path);
    dev->handle = handle;
    dev->read_pending = FALSE;
    dev->input_report_size = caps.InputReportByteLength;
    dev->output_report_size = caps.OutputReportByteLength;
    dev->feature_report_size = caps.FeatureReportByteLength;
    dev->input_buffer = (BYTE *)malloc(caps.InputReportByteLength);
    dev->output_buffer = (BYTE *)malloc(caps.OutputReportByteLength);
    dev->feature_buffer = (BYTE *)malloc(caps.FeatureReportByteLength);

    HidD_FreePreparsedData(pp_data);

    memset(&dev->input_ol, 0, sizeof(OVERLAPPED));
    dev->input_ol.hEvent = CreateEvent(&security, FALSE, FALSE, NULL);

    return dev;
}

INT hid_get_input_report(struct hid_device *device, DWORD timeout)
{
    DWORD bytes_read = 0;
    HANDLE ev = device->input_ol.hEvent;

    if (!device->read_pending)
    {
        device->read_pending = TRUE;
        memset(device->input_buffer, 0, device->input_report_size);
        ResetEvent(ev);
        if (!ReadFile(device->handle, device->input_buffer, device->input_report_size, &bytes_read, &device->input_ol))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                CancelIo(device->handle);
                device->read_pending = FALSE;
                return -1;
            }
        }
    }

    if (timeout >= 0)
    {
        if (WaitForSingleObject(ev, timeout) != WAIT_OBJECT_0)
        {
            /* There was no data this time. Return zero bytes available,
			   but leave the Overlapped I/O running. */
            return 0;
        }
    }

    /* Either WaitForSingleObject() told us that ReadFile has completed, or
	   we are in non-blocking mode. Get the number of bytes read. The actual
	   data has been copied to the data[] array which was passed to ReadFile(). */
    if (GetOverlappedResult(device->handle, &device->input_ol, &bytes_read, TRUE))
    {
        device->read_pending = FALSE;
        return bytes_read;
    }

    device->read_pending = FALSE;
    return -1;
}

INT hid_send_output_report(struct hid_device *device, const void *data, size_t length)
{
    DWORD bytes_written;
    OVERLAPPED ol;
    memset(&ol, 0, sizeof(OVERLAPPED));

    memset(&device->output_buffer, 0x0, device->output_report_size);
    memmove(&device->output_buffer, data, length > device->output_report_size ? device->output_report_size : length);

    if (!WriteFile(device->handle, device->output_buffer, device->output_report_size, &bytes_written, &ol))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            return -1;
        }
    }

    if (GetOverlappedResult(device->handle, &ol, &bytes_written, TRUE))
    {
        return bytes_written;
    }

    return -1;
}

INT hid_send_feature_report(struct hid_device *device, const void *data, size_t length)
{
    if (length <= device->feature_report_size)
    {
        if (HidD_SetFeature(device->handle, (PVOID)data, length))
        {
            return length;
        }
    }
    else
    {
        memmove(&device->feature_buffer, data, device->feature_report_size);
        if (HidD_SetFeature(device->handle, (PVOID)device->feature_buffer, device->feature_report_size))
        {
            return device->feature_report_size;
        }
    }
    return -1;
}

void hid_close_device(struct hid_device *device)
{
    CancelIo(device->handle);
    CloseHandle(device->input_ol.hEvent);
    CloseHandle(device->handle);
}

void hid_free_device(struct hid_device *device)
{
    free(device->path);
    free(device->input_buffer);
    free(device->output_buffer);
    free(device->feature_buffer);
    free(device);
}
