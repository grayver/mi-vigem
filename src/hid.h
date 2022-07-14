#ifndef HID_H
#define HID_H

#include <wtypes.h>

struct hid_device_info;

struct hid_device_info
{
    LPTSTR path;
    LPTSTR description;

    struct hid_device_info *next;
};

struct hid_device
{
    LPTSTR path;
    HANDLE handle;

    BOOLEAN read_pending;
    USHORT input_report_size;
    USHORT output_report_size;
    USHORT feature_report_size;
    BYTE *input_buffer;
    BYTE *output_buffer;
    BYTE *feature_buffer;

    OVERLAPPED input_ol;
};

GUID hid_get_class();
struct hid_device_info *hid_enumerate(const LPTSTR *path_filters);
BOOLEAN hid_reenable_device(LPTSTR path);
BOOLEAN check_vendor_and_product(LPTSTR path, USHORT vendor_id, USHORT product_id);
void hid_free_device_info(struct hid_device_info *device_info);
struct hid_device *hid_open_device(LPTSTR path, BOOLEAN access_rw, BOOLEAN shared);
INT hid_get_input_report(struct hid_device *device, DWORD timeout);
INT hid_send_output_report(struct hid_device *device, const void *data, size_t length);
INT hid_send_feature_report(struct hid_device *device, const void *data, size_t length);
void hid_close_device(struct hid_device *device);
void hid_free_device(struct hid_device *device);

#endif /* HID_H */
