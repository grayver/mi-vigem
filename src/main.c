#include <stdio.h>
#include <string.h>
#include <tchar.h>
#include <windows.h>
#include <synchapi.h>

#include <ViGEm/Client.h>

#include "tray.h"
#include "hid.h"
#include "mi.h"

#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

#define MAX_ACTIVE_DEVICE_COUNT 4
#define ACTIVE_DEVICE_MENU_TEMPLATE TEXT("#%d Xiaomi Gamepad (batt. %s)")
#define BATTERY_NA_TEXT TEXT("N/A")

struct active_device
{
    int index;
    struct hid_device *src_device;
    int src_gamepad_id;
    int src_battery_level;
    PVIGEM_TARGET tgt_device;
    XUSB_REPORT tgt_report;
    LPTSTR tray_text;
    struct tray_menu *tray_menu;
};

static int last_active_device_index = 0;
static int active_device_count = 0;
static struct active_device *active_devices[MAX_ACTIVE_DEVICE_COUNT];
static SRWLOCK active_devices_lock = SRWLOCK_INIT;
static PVIGEM_CLIENT vigem_client;
static BOOL vigem_connected = FALSE;

// future declarations
static void mi_gamepad_update_cb(int gamepad_id, struct mi_state *state);
static void mi_gamepad_stop_cb(int gamepad_id, BYTE break_reason);
static void CALLBACK x360_notification_cb(PVIGEM_CLIENT client, PVIGEM_TARGET target, UCHAR large_motor,
                                          UCHAR small_motor, UCHAR led_number, LPVOID user_data);
static void refresh_cb(struct tray_menu *item);
static void quit_cb(struct tray_menu *item);

static const struct tray_menu tray_menu_refresh = { .text = TEXT("Refresh"), .cb = refresh_cb };
static const struct tray_menu tray_menu_quit = { .text = TEXT("Quit"), .cb = quit_cb };
static const struct tray_menu tray_menu_separator = { .text = TEXT("-") };
static const struct tray_menu tray_menu_terminator = { .text = NULL };
static struct tray tray =
{
    .icon = TEXT("APP_ICON"),
    .tip = TEXT("Mi-ViGEm"),
    .menu = NULL
};

SHORT FORCEINLINE _map_byte_to_short(BYTE value, BOOL inverted)
{
    CHAR centered = value - 128;
    if (centered < -127)
    {
        centered = -127;
    }
    if (inverted)
    {
        centered = -centered;
    }
    return (SHORT)(32767 * centered / 127);
}

static void rebuild_tray_menu()
{
    struct tray_menu *prev_menu = tray.menu;

    AcquireSRWLockShared(&active_devices_lock);
    int dyn_item_count = active_device_count > 0 ? active_device_count + 1 : 0;
    struct tray_menu *new_menu = (struct tray_menu *)malloc((dyn_item_count + 3) * sizeof(struct tray_menu));
    int index = 0;
    for (; index < active_device_count; index++)
    {
        new_menu[index] = *active_devices[index]->tray_menu;
    }
    ReleaseSRWLockShared(&active_devices_lock);
    if (dyn_item_count > 0)
    {
        new_menu[index++] = tray_menu_separator;
    }
    new_menu[index++] = tray_menu_refresh;
    new_menu[index++] = tray_menu_quit;
    new_menu[index++] = tray_menu_terminator;

    tray.menu = new_menu;
    free(prev_menu);
}

static BOOL add_device(LPTSTR path)
{
    if (active_device_count == MAX_ACTIVE_DEVICE_COUNT)
    {
        tray_show_notification(NT_TRAY_WARNING, TEXT("Add new device"),
                               TEXT("Device count limit reached"));
        return FALSE;
    }

    struct hid_device *device = hid_open_device(path, TRUE, FALSE);
    if (device == NULL)
    {
        if (hid_reenable_device(path))
        {
            device = hid_open_device(path, TRUE, FALSE);
            if (device == NULL)
            {
                device = hid_open_device(path, TRUE, TRUE);
            }
        }
        else
        {
            device = hid_open_device(path, TRUE, TRUE);
        }
    }

    if (device == NULL)
    {
        tray_show_notification(NT_TRAY_WARNING, TEXT("Add new device"),
                               TEXT("Error opening new device"));
        return FALSE;
    }

    int mi_gamepad_id = mi_gamepad_start(device, mi_gamepad_update_cb, mi_gamepad_stop_cb);
    if (mi_gamepad_id < 0)
    {
        tray_show_notification(NT_TRAY_WARNING, TEXT("Add new device"),
                               TEXT("Error initializing new device"));
        hid_close_device(device);
        hid_free_device(device);
        return FALSE;
    }

    struct active_device *active_device = (struct active_device *)malloc(sizeof(struct active_device));
    active_device->src_device = device;
    active_device->index = ++last_active_device_index;
    active_device->src_gamepad_id = mi_gamepad_id;
    active_device->src_battery_level = -1;
    if (vigem_connected)
    {
        active_device->tgt_device = vigem_target_x360_alloc();
        vigem_target_add(vigem_client, active_device->tgt_device);
        XUSB_REPORT_INIT(&active_device->tgt_report);
        vigem_target_x360_register_notification(vigem_client, active_device->tgt_device, x360_notification_cb,
                                                (LPVOID)active_device);
    }
    int tray_text_length = _sctprintf(ACTIVE_DEVICE_MENU_TEMPLATE, active_device->index, BATTERY_NA_TEXT);
    active_device->tray_text = (LPTSTR)malloc((tray_text_length + 1) * sizeof(TCHAR));
    _stprintf(active_device->tray_text, ACTIVE_DEVICE_MENU_TEMPLATE, active_device->index, BATTERY_NA_TEXT);
    active_device->tray_menu = (struct tray_menu *)malloc(sizeof(struct tray_menu));
    memset(active_device->tray_menu, 0, sizeof(struct tray_menu));
    active_device->tray_menu->text = active_device->tray_text;

    AcquireSRWLockExclusive(&active_devices_lock);
    active_devices[active_device_count++] = active_device;
    ReleaseSRWLockExclusive(&active_devices_lock);

    rebuild_tray_menu();
    tray_update(&tray);
    if (vigem_connected)
    {
        tray_show_notification(NT_TRAY_INFO, TEXT("Add new device"),
                               TEXT("Device added successfully"));
    }
    else
    {
        tray_show_notification(NT_TRAY_WARNING, TEXT("Add new device"),
                               TEXT("Device added, but emulation doesn't work due to ViGEmBus problem"));
    }
    return TRUE;
}

static BOOL remove_device(int mi_gamepad_id)
{
    BOOL removed = FALSE;
    AcquireSRWLockExclusive(&active_devices_lock);
    for (int i = 0; i < active_device_count; i++)
    {
        if (active_devices[i]->src_gamepad_id == mi_gamepad_id)
        {
            hid_close_device(active_devices[i]->src_device);
            hid_free_device(active_devices[i]->src_device);
            if (vigem_connected)
            {
                vigem_target_x360_unregister_notification(active_devices[i]->tgt_device);
                vigem_target_remove(vigem_client, active_devices[i]->tgt_device);
                vigem_target_free(active_devices[i]->tgt_device);
            }
            free(active_devices[i]->tray_menu);
            free(active_devices[i]->tray_text);
            free(active_devices[i]);
            if (i < active_device_count - 1)
            {
                memmove(&active_devices[i], &active_devices[i + 1],
                        sizeof(struct active_device *) * (active_device_count - i - 1));
            }
            active_device_count--;
            removed = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&active_devices_lock);
    return removed;
}

static void refresh_devices()
{
    LPTSTR mi_hw_path_filters[3] = { MI_HW_FILTER_WIN10, MI_HW_FILTER_WIN7, NULL };
    struct hid_device_info *device_info = hid_enumerate(mi_hw_path_filters);
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
            if (_tcscmp(active_devices[i]->src_device->path, cur->path) == 0)
            {
                found = TRUE;
                break;
            }
            cur = cur->next;
        }
        if (!found)
        {
            mi_gamepad_stop(active_devices[i]->src_gamepad_id);
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
            if (_tcscmp(cur->path, active_devices[i]->src_device->path) == 0)
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
#ifdef UNICODE
    printf("Device operation %d with path %S\n", op, path);
#else
    printf("Device operation %d with path %s\n", op, path);
#endif
    fflush(stdout);
    refresh_devices();
}

static void mi_gamepad_update_cb(int gamepad_id, struct mi_state *state)
{
    struct active_device *active_device = NULL;
    AcquireSRWLockShared(&active_devices_lock);
    for (int i = 0; i < active_device_count; i++)
    {
        if (active_devices[i]->src_gamepad_id == gamepad_id)
        {
            active_device = active_devices[i];
            break;
        }
    }
    ReleaseSRWLockShared(&active_devices_lock);

    if (active_device == NULL)
    {
        return;
    }

    if (active_device->src_battery_level != state->battery)
    {
        active_device->src_battery_level = state->battery;
        TCHAR battery_buffer[5];
        _stprintf(battery_buffer, TEXT("%d%%"), active_device->src_battery_level);
        int tray_text_length = _sctprintf(ACTIVE_DEVICE_MENU_TEMPLATE, active_device->index, battery_buffer);
        free(active_device->tray_text);
        active_device->tray_text = (LPTSTR)malloc((tray_text_length + 1) * sizeof(TCHAR));
        _stprintf(active_device->tray_text, ACTIVE_DEVICE_MENU_TEMPLATE, active_device->index, battery_buffer);
        active_device->tray_menu->text = active_device->tray_text;
        rebuild_tray_menu();
        tray_update(&tray);
    }

    if (vigem_connected)
    {
        active_device->tgt_report.wButtons = 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_UP) != 0 ? XUSB_GAMEPAD_DPAD_UP : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_DOWN) != 0 ? XUSB_GAMEPAD_DPAD_DOWN : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_LEFT) != 0 ? XUSB_GAMEPAD_DPAD_LEFT : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_RIGHT) != 0 ? XUSB_GAMEPAD_DPAD_RIGHT : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_MENU) != 0 ? XUSB_GAMEPAD_START : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_RETURN) != 0 ? XUSB_GAMEPAD_BACK : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_LS) != 0 ? XUSB_GAMEPAD_LEFT_THUMB : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_RS) != 0 ? XUSB_GAMEPAD_RIGHT_THUMB : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_L1) != 0 ? XUSB_GAMEPAD_LEFT_SHOULDER : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_R1) != 0 ? XUSB_GAMEPAD_RIGHT_SHOULDER : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_A) != 0 ? XUSB_GAMEPAD_A : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_B) != 0 ? XUSB_GAMEPAD_B : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_X) != 0 ? XUSB_GAMEPAD_X : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_Y) != 0 ? XUSB_GAMEPAD_Y : 0;
        active_device->tgt_report.wButtons |= (state->buttons & MI_BUTTON_MI_BTN) != 0 ? XUSB_GAMEPAD_GUIDE : 0;
        active_device->tgt_report.bLeftTrigger = state->l2_trigger;
        active_device->tgt_report.bRightTrigger = state->r2_trigger;
        active_device->tgt_report.sThumbLX = _map_byte_to_short(state->left_stick_x, FALSE);
        active_device->tgt_report.sThumbLY = _map_byte_to_short(state->left_stick_y, TRUE);
        active_device->tgt_report.sThumbRX = _map_byte_to_short(state->right_stick_x, FALSE);
        active_device->tgt_report.sThumbRY = _map_byte_to_short(state->right_stick_y, TRUE);
        vigem_target_x360_update(vigem_client, active_device->tgt_device, active_device->tgt_report);
    }
}

static void mi_gamepad_stop_cb(int gamepad_id, BYTE break_reason)
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
    if (remove_device(gamepad_id))
    {
        rebuild_tray_menu();
        tray_update(&tray);
        tray_show_notification(ntf_type, TEXT("Remove device"), ntf_text);
    }
}

static void CALLBACK x360_notification_cb(PVIGEM_CLIENT client, PVIGEM_TARGET target, UCHAR large_motor,
                                          UCHAR small_motor, UCHAR led_number, LPVOID user_data)
{
    struct active_device *active_device = (struct active_device *)user_data;
    mi_gamepad_set_vibration(active_device->src_gamepad_id, small_motor, large_motor);
}

static void refresh_cb(struct tray_menu *item)
{
    (void)item;
    printf("Refresh\n");
    fflush(stdout);
    refresh_devices();
}

static void quit_cb(struct tray_menu *item)
{
    (void)item;
    printf("Quit\n");
    fflush(stdout);
    tray_exit();
}

int main()
{
    rebuild_tray_menu();
    if (tray_init(&tray) < 0)
    {
        printf("Failed to create tray\n");
        return 1;
    }
    vigem_client = vigem_alloc();
    VIGEM_ERROR vigem_res = vigem_connect(vigem_client);
    if (vigem_res == VIGEM_ERROR_BUS_NOT_FOUND)
    {
        tray_show_notification(NT_TRAY_ERROR, TEXT("ViGEmBus connection"),
                               TEXT("ViGEmBus not installed"));
    }
    else if (vigem_res == VIGEM_ERROR_BUS_VERSION_MISMATCH)
    {
        tray_show_notification(NT_TRAY_ERROR, TEXT("ViGEmBus connection"),
                               TEXT("ViGEmBus incompatible version"));
    }
    else if (vigem_res != VIGEM_ERROR_NONE)
    {
        tray_show_notification(NT_TRAY_ERROR, TEXT("ViGEmBus connection"),
                               TEXT("Error connecting to ViGEmBus"));
    }
    else
    {
        vigem_connected = TRUE;
    }
    refresh_devices();
    tray_register_device_notification(hid_get_class(), device_change_cb);

    while (tray_loop(TRUE) == 0)
    {
        ;
    }

    AcquireSRWLockExclusive(&active_devices_lock);
    for (int i = 0; i < active_device_count; i++)
    {
        hid_close_device(active_devices[i]->src_device);
        hid_free_device(active_devices[i]->src_device);
        if (vigem_connected)
        {
            vigem_target_x360_unregister_notification(active_devices[i]->tgt_device);
            vigem_target_remove(vigem_client, active_devices[i]->tgt_device);
            vigem_target_free(active_devices[i]->tgt_device);
        }
        free(active_devices[i]->tray_menu);
        free(active_devices[i]->tray_text);
        free(active_devices[i]);
    }
    active_device_count = 0;
    ReleaseSRWLockExclusive(&active_devices_lock);
    if (vigem_connected)
    {
        vigem_disconnect(vigem_client);
    }
    vigem_free(vigem_client);
    free(tray.menu);
    return 0;
}
