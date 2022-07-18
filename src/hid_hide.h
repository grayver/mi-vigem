#ifndef HID_HIDE_H
#define HID_HIDE_H

#include <wtypes.h>

#include "hid.h"

#define HID_HIDE_HW_PATH TEXT("\\\\.\\HidHide")

#define HID_HIDE_RESULT_OK         0x0000
#define HID_HIDE_NOT_INSTALLED     0x0001
#define HID_HIDE_INIT_ERROR        0x0002
#define HID_HIDE_NOT_INITIALIZED   0x0004

int hid_hide_init();
int hid_hide_bind(struct hid_device_info *dev_info);
void hid_hide_unbind(struct hid_device_info *dev_info);
void hid_hide_free();

#endif /* HID_HIDE_H */
