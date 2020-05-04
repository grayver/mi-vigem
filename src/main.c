#include <stdio.h>
#include <string.h>
#include <tchar.h>
#include <windows.h>
#include <synchapi.h>

#include "tray.h"
#include "hid.h"
#include "mi.h"

#define MAX_DEVICE_COUNT 16

static int active_device_count = 0;
static struct hid_device *active_devices[MAX_DEVICE_COUNT];
static SRWLOCK active_devices_lock = SRWLOCK_INIT;

// future declarations
static void mi_gamepad_update_cb(struct hid_device *device, struct mi_state *state);
static void mi_gamepad_stop_cb(struct hid_device *device, BYTE break_reason);
static void quit_cb(struct tray_menu *item);

static const struct tray_menu tray_menu_quit = { .text = "Quit", .cb = quit_cb };
static const struct tray_menu tray_menu_separator = { .text = "-" };
static const struct tray_menu tray_menu_terminator = { .text = NULL };
static struct tray tray =
{
    .icon = TEXT("APP_ICON"),
    .tip = TEXT("Mi-ViGEm")
};

static struct hid_device *add_device(LPTSTR path)
{
    if (active_device_count == MAX_DEVICE_COUNT)
    {
        tray_show_notification(NT_TRAY_WARNING, TEXT("Add new device"),
                               TEXT("Device count limit reached"));
        return NULL;
    }

    struct hid_device *device = hid_open_device(path, FALSE);
    if (device == NULL)
    {
        if (hid_reenable_device(path))
        {
            device = hid_open_device(path, FALSE);
            if (device == NULL)
            {
                device = hid_open_device(path, TRUE);
            }
        }
        else
        {
            device = hid_open_device(path, TRUE);
        }
    }

    if (device == NULL)
    {
        tray_show_notification(NT_TRAY_ERROR, TEXT("Add new device"),
                               TEXT("Error opening new device"));
        return NULL;
    }

    AcquireSRWLockExclusive(&active_devices_lock);
    active_devices[active_device_count++] = device;
    ReleaseSRWLockExclusive(&active_devices_lock);

    mi_gamepad_start(device, mi_gamepad_update_cb, mi_gamepad_stop_cb);
    // TODO: add to tray
    // TODO: show notification
    return device;
}

static void remove_device(int index)
{
    AcquireSRWLockExclusive(&active_devices_lock);
    hid_close_device(active_devices[index]);
    hid_free_device(active_devices[index]);
    if (index < active_device_count - 1)
    {
        memmove(&active_devices[index], &active_devices[index + 1],
                sizeof(struct hid_device *) * (active_device_count - index - 1));
    }
    active_device_count--;
    ReleaseSRWLockExclusive(&active_devices_lock);
}

static void refresh_devices()
{
    struct hid_device_info *device_info = hid_enumerate(MI_HW_FILTER);
    struct hid_device_info *cur;
    BOOL found = FALSE;

    // remove missing devices
    AcquireSRWLockShared(&active_devices_lock);
    for (int i = 0; i < active_device_count; i++)
    {
        found = FALSE;
        cur = device_info;
        while (cur != NULL)
        {
            if (_tcscmp(active_devices[i]->path, cur->path) == 0)
            {
                found = TRUE;
                break;
            }
            cur = cur->next;
        }
        if (!found)
        {
            mi_gamepad_stop(active_devices[i]);
        }
    }
    ReleaseSRWLockShared(&active_devices_lock);

    // add new devices
    cur = device_info;
    while (cur != NULL)
    {
        found = FALSE;
        AcquireSRWLockShared(&active_devices_lock);
        for (int i = 0; i < active_device_count; i++)
        {
            if (_tcscmp(cur->path, active_devices[i]->path) == 0)
            {
                found = TRUE;
                break;
            }
        }
        ReleaseSRWLockShared(&active_devices_lock);
        if (!found)
        {
            add_device(cur->path);
        }
        cur = cur->next;
    }

    // free hid_device_info list
    while (device_info)
    {
        cur = device_info->next;
        hid_free_device_info(device_info);
        device_info = cur;
    }
}

static void device_change_cb(UINT op, LPTSTR path)
{
    // refresh devices regardless operation type
    printf("Device operation %d with path %s\n", op, path);
    refresh_devices();
}

static void mi_gamepad_update_cb(struct hid_device *device, struct mi_state *state)
{
    //
}

static void mi_gamepad_stop_cb(struct hid_device *device, BYTE break_reason)
{
    UINT ntf_type = break_reason == MI_BREAK_REASON_REQUESTED ? NT_TRAY_INFO : NT_TRAY_WARNING;
    LPTSTR ntf_text;
    switch (break_reason)
    {
    case MI_BREAK_REASON_REQUESTED:
        ntf_text = TEXT("Device removed successfully");
        break;
    case MI_BREAK_REASON_INIT_ERROR:
        ntf_text = TEXT("Error initializing device");
        break;
    case MI_BREAK_REASON_READ_ERROR:
        ntf_text = TEXT("Error reading data");
        break;
    case MI_BREAK_REASON_WRITE_ERROR:
        ntf_text = TEXT("Error writing data");
        break;
    default:
        ntf_text = TEXT("Unknown error");
        break;
    }
    tray_show_notification(ntf_type, TEXT("Remove device"), ntf_text);

    int index = -1;
    AcquireSRWLockShared(&active_devices_lock);
    for (int i = 0; i < active_device_count; i++)
    {
        if (_tcscmp(device->path, active_devices[i]->path) == 0)
        {
            index = i;
            break;
        }
    }
    ReleaseSRWLockShared(&active_devices_lock);
    if (index > -1)
    {
        remove_device(index);
    }
}

static void quit_cb(struct tray_menu *item)
{
    (void)item;
    printf("Quit\n");
    tray_exit();
}

int main()
{
    tray.menu = malloc(2 * sizeof(struct tray_menu));
    tray.menu[0] = tray_menu_quit;
    tray.menu[1] = tray_menu_terminator;

    if (tray_init(&tray) < 0)
    {
        printf("Failed to create tray\n");
        return 1;
    }
    refresh_devices();
    tray_register_device_notification(hid_get_class(), device_change_cb);
    while (tray_loop(TRUE) == 0)
    {
        ;
    }
    return 0;
}
