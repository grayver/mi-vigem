#include <tchar.h>
#include <windows.h>

#include "hid_hide.h"

#pragma comment(lib, "kernel32.lib")

// The Hid Hide I/O control custom device type (range 32768 .. 65535)
#define IO_CONTROL_DEVICE_TYPE 32769u

// The Hid Hide I/O control codes
#define IOCTL_GET_WHITELIST CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2048, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_WHITELIST CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2049, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_BLACKLIST CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2050, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_BLACKLIST CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2051, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_ACTIVE    CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2052, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_ACTIVE    CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2053, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_WLINVERSE CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2054, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_WLINVERSE CTL_CODE(IO_CONTROL_DEVICE_TYPE, 2055, METHOD_BUFFERED, FILE_READ_DATA)

struct hh_str_arr;

struct hh_str_arr
{
    LPTSTR value;

    struct hh_str_arr *next;
};

struct hid_hide_ctx
{
    HANDLE handle;

    BOOLEAN active;
    struct hh_str_arr *black_list;
    struct hh_str_arr *white_list;
    BOOLEAN inverse;
};

static struct hid_hide_ctx *hh_ctx = NULL;

static struct hh_str_arr* _hh_multi_string_to_arr(LPWSTR input_str, DWORD input_len)
{
    struct hh_str_arr *head = NULL, *tail = NULL;
    for (DWORD i = 0, start = 0; i < input_len; i++)
    {
        if (input_str[i] == 0)
        {
            if (i > start)
            {
                struct hh_str_arr *cur = (struct hh_str_arr *)malloc(sizeof(struct hh_str_arr));
#ifdef UNICODE
                cur->value = (LPWSTR)malloc((i - start + 1) * sizeof(WCHAR));
                wcscpy(cur->value, &input_str[start]);
#else
                int value_size = WideCharToMultiByte(CP_ACP, 0, &input_str[start], -1, NULL, 0, NULL, NULL);
                cur->value = (LPSTR)malloc(value_size);
                WideCharToMultiByte(CP_ACP, 0, &input_str[start], -1, cur->value, value_size, NULL, NULL);
#endif /* UNICODE */
                cur->next = NULL;

                if (head == NULL)
                {
                    head = cur;
                }
                else
                {
                    tail->next = cur;
                }
                tail = cur;
            }
            start = i + 1;
        }
    }
    return head;
}

static LPWSTR _hh_arr_to_multi_string(struct hh_str_arr* input_arr, LPDWORD output_len)
{
    struct hh_str_arr *cur = input_arr;
    LPWSTR buffer = NULL;
    DWORD allocated_len = 0;
    while (cur)
    {
        size_t len_to_alloc = _tcslen(cur->value) + 1;
        buffer = (LPWSTR)realloc(buffer, (allocated_len + len_to_alloc) * sizeof(WCHAR));
#ifdef UNICODE
        wcscpy(&buffer[allocated_len], cur->value);
#else
        MultiByteToWideChar(CP_ACP, 0, cur->value, -1, &buffer[allocated_len], len_to_alloc);
#endif /* UNICODE */
        allocated_len += len_to_alloc;
        cur = cur->next;
    }
    *output_len = allocated_len;
    return buffer;
}

static BOOLEAN _hh_in_str_arr(struct hh_str_arr* arr, LPTSTR value)
{
    while (arr)
    {
        if (_tcscmp(arr->value, value) == 0)
        {
            return TRUE;
        }
        arr = arr->next;
    }
    return FALSE;
}

static void _hh_free_str_arr(struct hh_str_arr* arr)
{
    struct hh_str_arr* cur;
    while (arr)
    {
        cur = arr->next;
        free(arr->value);
        free(arr);
        arr = cur;
    }
}

static BOOLEAN _hh_get_active(HANDLE dev_handle)
{
    DWORD bytes_returned = 0;
    BOOLEAN output_buffer[1];
    if (!DeviceIoControl(dev_handle, IOCTL_GET_ACTIVE, NULL, 0, output_buffer, sizeof(output_buffer), &bytes_returned, NULL))
    {
        return FALSE;
    }
    if (bytes_returned != sizeof(output_buffer))
    {
        return FALSE;
    }
    return (output_buffer[0] != FALSE);
}

static void _hh_set_active(HANDLE dev_handle, BOOLEAN active)
{
    DWORD bytes_returned = 0;
    BOOLEAN input_buffer[1];
    input_buffer[0] = (active ? TRUE : FALSE);
    DeviceIoControl(dev_handle, IOCTL_SET_ACTIVE, input_buffer, sizeof(input_buffer), NULL, 0, &bytes_returned, NULL);
}

static struct hh_str_arr* _hh_get_black_list(HANDLE dev_handle)
{
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(dev_handle, IOCTL_GET_BLACKLIST, NULL, 0, NULL, 0, &bytes_returned, NULL))
    {
        return NULL;
    }
    LPWSTR output_buffer = (LPWSTR)malloc(bytes_returned);
    if (!DeviceIoControl(dev_handle, IOCTL_GET_BLACKLIST, NULL, 0, output_buffer, bytes_returned, &bytes_returned, NULL))
    {
        return NULL;
    }
    struct hh_str_arr* arr = _hh_multi_string_to_arr(output_buffer, bytes_returned / sizeof(WCHAR));
    free(output_buffer);
    return arr;
}

static void _hh_set_black_list(HANDLE dev_handle, struct hh_str_arr* dev_paths)
{
    DWORD bytes_returned = 0, chars_returned = 0;
    LPWSTR input_buffer = _hh_arr_to_multi_string(dev_paths, &chars_returned);
    bytes_returned = chars_returned * sizeof(WCHAR);
    DeviceIoControl(dev_handle, IOCTL_SET_BLACKLIST, input_buffer, bytes_returned, NULL, 0, &bytes_returned, NULL);
    free(input_buffer);
}

static struct hh_str_arr* _hh_get_white_list(HANDLE dev_handle)
{
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(dev_handle, IOCTL_GET_WHITELIST, NULL, 0, NULL, 0, &bytes_returned, NULL))
    {
        return NULL;
    }
    LPWSTR output_buffer = (LPWSTR)malloc(bytes_returned);
    if (!DeviceIoControl(dev_handle, IOCTL_GET_WHITELIST, NULL, 0, output_buffer, bytes_returned, &bytes_returned, NULL))
    {
        return NULL;
    }
    struct hh_str_arr* arr = _hh_multi_string_to_arr(output_buffer, bytes_returned / sizeof(WCHAR));
    free(output_buffer);
    return arr;
}

static void _hh_set_white_list(HANDLE dev_handle, struct hh_str_arr* image_names)
{
    DWORD bytes_returned = 0, chars_returned = 0;
    LPWSTR input_buffer = _hh_arr_to_multi_string(image_names, &chars_returned);
    bytes_returned = chars_returned * sizeof(WCHAR);
    DeviceIoControl(dev_handle, IOCTL_SET_WHITELIST, input_buffer, bytes_returned, NULL, 0, &bytes_returned, NULL);
    free(input_buffer);
}

static BOOLEAN _hh_get_inverse(HANDLE dev_handle)
{
    DWORD bytes_returned = 0;
    BOOLEAN output_buffer[1];
    if (!DeviceIoControl(dev_handle, IOCTL_GET_WLINVERSE, NULL, 0, output_buffer, sizeof(output_buffer), &bytes_returned, NULL))
    {
        return FALSE;
    }
    if (bytes_returned != sizeof(output_buffer))
    {
        return FALSE;
    }
    return (output_buffer[0] != FALSE);
}

static void _hh_set_inverse(HANDLE dev_handle, BOOLEAN inverse)
{
    DWORD bytes_returned = 0;
    BOOLEAN input_buffer[1];
    input_buffer[0] = (inverse ? TRUE : FALSE);
    DeviceIoControl(dev_handle, IOCTL_SET_WLINVERSE, input_buffer, sizeof(input_buffer), NULL, 0, &bytes_returned, NULL);
}

static LPTSTR _hh_get_app_image_name()
{
    LPTSTR result = NULL;
    WCHAR module_filename[UNICODE_STRING_MAX_CHARS];
    GetModuleFileNameW(NULL, module_filename, UNICODE_STRING_MAX_CHARS);
    LPWSTR mount_point = NULL;
    WCHAR volume_name[UNICODE_STRING_MAX_CHARS];
    HANDLE fv_handle = FindFirstVolumeW(volume_name, UNICODE_STRING_MAX_CHARS);
    if (fv_handle != INVALID_HANDLE_VALUE)
    {
        while (TRUE)
        {
            DWORD chars_returned;
            WCHAR volume_path_names[UNICODE_STRING_MAX_CHARS];
            if (GetVolumePathNamesForVolumeNameW(volume_name, volume_path_names, UNICODE_STRING_MAX_CHARS, &chars_returned))
            {
                for (DWORD i = 0, start = 0; i < chars_returned; i++)
                {
                    if (volume_path_names[i] == 0)
                    {
                        if (i > start)
                        {
                            if ((wcsncmp(module_filename, &volume_path_names[start], i - start) == 0)
                                && (mount_point == NULL || i - start > wcslen(mount_point)))
                            {
                                mount_point = (LPWSTR)realloc(mount_point, (i - start + 1) * sizeof(WCHAR));
                                wcscpy(mount_point, &volume_path_names[start]);
                            }
                        }
                        start = i + 1;
                    }
                }
            }
            if (!FindNextVolumeW(fv_handle, volume_name, UNICODE_STRING_MAX_CHARS))
            {
                break;
            }
        }
        FindVolumeClose(fv_handle);
    }
    if (mount_point != NULL && GetVolumeNameForVolumeMountPointW(mount_point, volume_name, UNICODE_STRING_MAX_CHARS))
    {
        WCHAR dos_device_name[UNICODE_STRING_MAX_CHARS], stripped_volume_name[UNICODE_STRING_MAX_CHARS];
        // Strip the leading '\\?\' and trailing '\' and isolate the Volume{} part in the volume name
        wcsncpy(stripped_volume_name, volume_name + 4, wcslen(volume_name) - 5);
        if (QueryDosDeviceW(stripped_volume_name, dos_device_name, UNICODE_STRING_MAX_CHARS))
        {
            WCHAR filename_wo_mount_point[UNICODE_STRING_MAX_CHARS];
            wcscpy(filename_wo_mount_point, module_filename + wcslen(mount_point));
            LPWSTR result_w = (LPWSTR)malloc((wcslen(dos_device_name) +1 + wcslen(filename_wo_mount_point)) * sizeof(WCHAR));
            wcscat(wcscat(wcscpy(result, dos_device_name), "\\"), filename_wo_mount_point);
#ifdef UNICODE
            result = result_w;
#else
            int result_size = WideCharToMultiByte(CP_ACP, 0, result_w, -1, NULL, 0, NULL, NULL);
            result = (LPSTR)malloc(result_size);
            WideCharToMultiByte(CP_ACP, 0, result_w, -1, result, result_size, NULL, NULL);
            free(result_w);
#endif /* UNICODE */
        }
        free(mount_point);
    }
    return result;
}

int hid_hide_init()
{
    HANDLE handle = CreateFile(HID_HIDE_HW_PATH, GENERIC_READ, (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            return HID_HIDE_NOT_INSTALLED; // HidHide driver not installed
        }
        else
        {
            return HID_HIDE_INIT_ERROR; // other error
        }
    }

    hh_ctx = (struct hid_hide_ctx *)malloc(sizeof(struct hid_hide_ctx));
    hh_ctx->handle = handle;
    hh_ctx->active = _hh_get_active(handle);
    hh_ctx->black_list = _hh_get_black_list(handle);
    hh_ctx->white_list = _hh_get_white_list(handle);
    hh_ctx->inverse = _hh_get_inverse(handle);

    // check if our app whitelisted
    LPTSTR app_image_name = _hh_get_app_image_name();
    BOOLEAN app_whitelisted = _hh_in_str_arr(hh_ctx->white_list, app_image_name);
    if (!hh_ctx->inverse && !app_whitelisted)
    {
        // add app to the white list
        struct hh_str_arr *app_el = (struct hh_str_arr *)malloc(sizeof(struct hh_str_arr));
        app_el->value = app_image_name;
        app_el->next = hh_ctx->white_list;
        hh_ctx->white_list = app_el;
        _hh_set_white_list(hh_ctx->handle, hh_ctx->white_list);
    }
    else if (hh_ctx->inverse && app_whitelisted)
    {
        // remove app from the white list
        struct hh_str_arr *cur = hh_ctx->white_list, *prev = NULL;
        while (cur)
        {
            if (_tcscmp(cur->value, app_image_name) == 0)
            {
                if (prev != NULL)
                {
                    prev->next = cur->next;
                }
                else
                {
                    hh_ctx->white_list = cur->next;
                }
                free(cur->value);
                free(cur);
                break;
            }
            prev = cur;
            cur = cur->next;
        }
        _hh_set_white_list(hh_ctx->handle, hh_ctx->white_list);
        free(app_image_name);
    }
    else
    {
        free(app_image_name);
    }

    return HID_HIDE_RESULT_OK;
}

int hid_hide_bind(LPTSTR dev_path)
{
    if (hh_ctx != NULL)
    {
        struct hh_str_arr *cur = hh_ctx->black_list, *prev = NULL;
        while (cur)
        {
            if (_tcscmp(cur->value, dev_path) == 0)
            {
                return HID_HIDE_RESULT_OK; // already in black list
            }
            prev = cur;
            cur = cur->next;
        }

        struct hh_str_arr *dev_el = (struct hh_str_arr *)malloc(sizeof(struct hh_str_arr));
        dev_el->value = (LPTSTR)malloc((_tcslen(dev_path) + 1) * sizeof(TCHAR));
        _tcscpy(dev_el->value, dev_path);
        dev_el->next = NULL;
        prev->next = dev_el;

        _hh_get_black_list(hh_ctx->handle, hh_ctx->black_list);

        return HID_HIDE_RESULT_OK;
    }

    return HID_HIDE_NOT_INITIALIZED; // HidHide wasn't properly initialized
}

void hid_hide_unbind(LPTSTR dev_path)
{
    if (hh_ctx != NULL)
    {
        struct hh_str_arr *cur = hh_ctx->black_list, *prev = NULL;
        while (cur)
        {
            if (_tcscmp(cur->value, dev_path) == 0)
            {
                if (prev != NULL)
                {
                    prev->next = cur->next;
                }
                else
                {
                    hh_ctx->black_list = cur->next;
                }
                free(cur->value);
                free(cur);
                break;
            }
            prev = cur;
            cur = cur->next;
        }
        _hh_get_black_list(hh_ctx->handle, hh_ctx->black_list);

        return HID_HIDE_RESULT_OK;
    }

    return HID_HIDE_NOT_INITIALIZED; // HidHide wasn't properly initialized
}

void hid_hide_free()
{
    if (hh_ctx != NULL)
    {
        CloseHandle(hh_ctx->handle);
        if (hh_ctx->black_list != NULL)
        {
            _hh_free_str_arr(hh_ctx->black_list);
        }
        if (hh_ctx->white_list != NULL)
        {
            _hh_free_str_arr(hh_ctx->white_list);
        }
        free(hh_ctx);
    }
}
