#include <tchar.h>
#include <initguid.h>
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <devguid.h>

#include "hid.h"
#include "utils.h"

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

static BOOLEAN got_hid_interface_guid = FALSE;
static GUID hid_interface_guid;

static void _hid_fill_symlink_and_desc(struct hid_device_info *dev_info)
{
    GUID interface_guid = hid_get_interface_guid();
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA devinfo_data;
    SP_DEVICE_INTERFACE_DATA device_interface_data;
    SP_DEVICE_INTERFACE_DETAIL_DATA *device_interface_detail_data = NULL;
    DWORD required_size = 0;
    DEVPROPTYPE prop_type;
    LPTSTR desc_buffer = NULL;
    LPWSTR desc_buffer_w = NULL;

    memset(&devinfo_data, 0x0, sizeof(devinfo_data));
    devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
    device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    device_info_set = SetupDiGetClassDevs(&interface_guid, dev_info->instance_path, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD device_index = 0;
    while (SetupDiEnumDeviceInfo(device_info_set, device_index, &devinfo_data))
    {
        DWORD device_interface_index = 0;
        while (SetupDiEnumDeviceInterfaces(device_info_set, &devinfo_data, &interface_guid, device_interface_index, &device_interface_data))
        {
            SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, NULL, 0, &required_size, NULL);
            device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA *)malloc(required_size);
            device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, device_interface_detail_data, required_size, NULL, NULL))
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
                                                         NULL, NULL, 0, &required_size)
                        || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
                    {
                        desc_buffer = (LPTSTR)malloc(required_size);
                        memset(desc_buffer, 0, required_size);
                        SetupDiGetDeviceRegistryProperty(device_info_set, &devinfo_data, SPDRP_DEVICEDESC,
                                                         NULL, (PBYTE)desc_buffer, required_size, NULL);
                    }
                }

                dev_info->symlink = (LPTSTR)malloc((_tcslen(device_interface_detail_data->DevicePath) + 1) * sizeof(TCHAR));
                _tcscpy(dev_info->symlink, device_interface_detail_data->DevicePath);
                dev_info->description = desc_buffer;
            }

            free(device_interface_detail_data);

            device_interface_index++;
        }

        device_index++;
    }

    SetupDiDestroyDeviceInfoList(device_info_set);
}

static void _hid_fill_container_path(struct hid_device_info *dev_info)
{
    DEVINST dev_inst;
    DEVINST dev_inst_parent;
    DEVPROPTYPE prop_type;
    ULONG required_size = 0;
    
    if (CM_Locate_DevNode(&dev_inst, dev_info->instance_path, CM_LOCATE_DEVNODE_PHANTOM) == CR_SUCCESS)
    {
        if (CM_Get_Parent(&dev_inst_parent, dev_inst, 0) == CR_SUCCESS)
        {
            CM_Get_DevNode_PropertyW(dev_inst_parent, &DEVPKEY_Device_InstanceId, &prop_type, NULL, &required_size, 0);
            LPWSTR container_path_w = (LPWSTR)malloc(required_size);
            if (CM_Get_DevNode_PropertyW(dev_inst_parent, &DEVPKEY_Device_InstanceId, &prop_type, (PBYTE)container_path_w, &required_size, 0) == CR_SUCCESS
                && prop_type == DEVPROP_TYPE_STRING)
            {
#ifdef UNICODE
                dev_info->container_instance_path = container_path_w;
#else
                int container_path_size = WideCharToMultiByte(CP_ACP, 0, container_path_w, -1, NULL, 0, NULL, NULL);
                dev_info->container_instance_path = (LPSTR)malloc(container_path_size);
                WideCharToMultiByte(CP_ACP, 0, container_path_w, -1, dev_info->container_instance_path, container_path_size, NULL, NULL);
                free(container_path_w);
#endif /* UNICODE */
            }
            else
            {
                free(container_path_w);
            }
        }
    }
}

GUID hid_get_interface_guid()
{
    if (!got_hid_interface_guid)
    {
        HidD_GetHidGuid(&hid_interface_guid);
        got_hid_interface_guid = TRUE;
    }
    return hid_interface_guid;
}

struct hid_device_info *hid_enumerate(const LPTSTR *path_filters)
{
    struct hid_device_info *root_dev = NULL;
    struct hid_device_info *cur_dev = NULL;

    LPTSTR class_filter = _guid_to_str(&GUID_DEVCLASS_HIDCLASS);
    DWORD required_size = 0;

    if (CM_Get_Device_ID_List_Size(&required_size, class_filter, CM_GETIDLIST_FILTER_CLASS) == CR_SUCCESS)
    {
        LPTSTR dev_id_list_buffer = (LPTSTR)malloc(required_size * sizeof(TCHAR));
        if (CM_Get_Device_ID_List(class_filter, dev_id_list_buffer, required_size, CM_GETIDLIST_FILTER_CLASS) == CR_SUCCESS)
        {
            for (DWORD i = 0, start = 0; i < required_size; i++)
            {
                if (dev_id_list_buffer[i] == 0)
                {
                    if (i > start)
                    {
                        BOOLEAN matched = TRUE;
                        if (path_filters != NULL)
                        {
                            matched = FALSE;
                            for (const LPTSTR *pfilter = path_filters; *pfilter != NULL; pfilter++)
                            {
                                if (_tcsistr(&dev_id_list_buffer[start], *pfilter) != NULL)
                                {
                                    matched = TRUE;
                                    break;
                                }
                            }
                        }

                        if (matched)
                        {
                            struct hid_device_info *dev = (struct hid_device_info *)malloc(sizeof(struct hid_device_info));
                            memset(dev, 0, sizeof(struct hid_device_info));
                            dev->instance_path = (LPTSTR)malloc((i - start + 1) * sizeof(TCHAR));
                            _tcscpy(dev->instance_path, &dev_id_list_buffer[start]);
                            _hid_fill_symlink_and_desc(dev);
                            _hid_fill_container_path(dev);
                            dev->next = NULL;

                            if (dev->symlink != NULL)
                            {
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
                            else
                            {
                                hid_free_device_info(dev);
                            }
                        }
                    }
                    start = i + 1;
                }
            }
        }
    }

    free(class_filter);
    return root_dev;
}

BOOLEAN hid_reenable_device(struct hid_device_info *device_info)
{
    GUID interface_guid = hid_get_interface_guid();
    SP_DEVINFO_DATA devinfo_data;
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    DWORD required_size = 0;

    memset(&devinfo_data, 0x0, sizeof(devinfo_data));
    devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);

    device_info_set = SetupDiGetClassDevs(&interface_guid, device_info->instance_path, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    if (!SetupDiEnumDeviceInfo(device_info_set, 0, &devinfo_data) || SetupDiEnumDeviceInfo(device_info_set, 1, &devinfo_data))
    {
        SetupDiDestroyDeviceInfoList(device_info_set);
        return FALSE;
    }

    SP_PROPCHANGE_PARAMS pc_params =
    {
        .ClassInstallHeader =
        {
            .cbSize = sizeof(SP_CLASSINSTALL_HEADER),
            .InstallFunction = DIF_PROPERTYCHANGE
        },
        .StateChange = DICS_DISABLE,
        .Scope = DICS_FLAG_GLOBAL,
        .HwProfile = 0
    };
    BOOLEAN res;
    res = SetupDiSetClassInstallParams(device_info_set, &devinfo_data, (PSP_CLASSINSTALL_HEADER)&pc_params,
                                       sizeof(SP_PROPCHANGE_PARAMS));
    res = res && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, device_info_set, &devinfo_data);
    pc_params.StateChange = DICS_ENABLE;
    res = res && SetupDiSetClassInstallParams(device_info_set, &devinfo_data, (PSP_CLASSINSTALL_HEADER)&pc_params,
                                              sizeof(SP_PROPCHANGE_PARAMS));
    res = res && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, device_info_set, &devinfo_data);

    SetupDiDestroyDeviceInfoList(device_info_set);
    return res;
}

BOOLEAN check_vendor_and_product(struct hid_device_info *device_info, USHORT vendor_id, USHORT product_id)
{
    HANDLE dev_handle = CreateFile(device_info->symlink, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (dev_handle != INVALID_HANDLE_VALUE)
    {
        BOOLEAN matched = FALSE;
        HIDD_ATTRIBUTES attributes =
        {
            .Size = sizeof(HIDD_ATTRIBUTES)
        };
        if (HidD_GetAttributes(dev_handle, &attributes))
        {
            matched = (vendor_id == 0x0 || attributes.VendorID == vendor_id) && (product_id == 0x0 || attributes.ProductID == product_id);
        }
        CloseHandle(dev_handle);
        return matched;
    }
    else
    {
        return FALSE;
    }
}

struct hid_device_info *hid_clone_device_info(struct hid_device_info *device_info)
{
    struct hid_device_info *result = (struct hid_device_info *)malloc(sizeof(struct hid_device_info));
    memset(result, 0, sizeof(struct hid_device_info));
    if (device_info->instance_path != NULL)
    {
        result->instance_path = (LPTSTR)malloc((_tcslen(device_info->instance_path) + 1) * sizeof(TCHAR));
        _tcscpy(result->instance_path, device_info->instance_path);
    }
    if (device_info->container_instance_path != NULL)
    {
        result->container_instance_path = (LPTSTR)malloc((_tcslen(device_info->container_instance_path) + 1) * sizeof(TCHAR));
        _tcscpy(result->container_instance_path, device_info->container_instance_path);
    }
    if (device_info->symlink != NULL)
    {
        result->symlink = (LPTSTR)malloc((_tcslen(device_info->symlink) + 1) * sizeof(TCHAR));
        _tcscpy(result->symlink, device_info->symlink);
    }
    if (device_info->description != NULL)
    {
        result->description = (LPTSTR)malloc((_tcslen(device_info->description) + 1) * sizeof(TCHAR));
        _tcscpy(result->description, device_info->description);
    }
    return result;
}

void hid_free_device_info(struct hid_device_info *device_info)
{
    free(device_info->instance_path);
    free(device_info->container_instance_path);
    free(device_info->symlink);
    free(device_info->description);
    free(device_info);
}

struct hid_device *hid_open_device(struct hid_device_info *device_info, BOOLEAN access_rw, BOOLEAN shared)
{
    DWORD desired_access = access_rw ? (GENERIC_WRITE | GENERIC_READ) : 0;
    DWORD share_mode = shared ? (FILE_SHARE_READ | FILE_SHARE_WRITE) : 0;
    SECURITY_ATTRIBUTES security =
    {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle = TRUE
    };
    HANDLE handle = CreateFile(device_info->symlink, desired_access, share_mode, &security, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
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
    dev->device_info = hid_clone_device_info(device_info);
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

    memset(device->output_buffer, 0x0, device->output_report_size);
    memmove(device->output_buffer, data, length > device->output_report_size ? device->output_report_size : length);

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
        memset(device->feature_buffer, 0, device->feature_report_size);
        memmove(device->feature_buffer, data, length);

    }
    else
    {
        memmove(device->feature_buffer, data, device->feature_report_size);
    }
    if (HidD_SetFeature(device->handle, (PVOID)device->feature_buffer, device->feature_report_size))
    {
        return length < device->feature_report_size ? length : device->feature_report_size;
    }
    return -1;
}

void hid_close_device(struct hid_device *device)
{
    CancelIoEx(device->handle, NULL);
    CloseHandle(device->input_ol.hEvent);
    CloseHandle(device->handle);
}

void hid_free_device(struct hid_device *device)
{
    hid_free_device_info(device->device_info);
    free(device->input_buffer);
    free(device->output_buffer);
    free(device->feature_buffer);
    free(device);
}
