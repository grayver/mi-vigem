#include <tchar.h>

#include "mi.h"
#include "hid.h"

#define MI_READ_ATTEMPT_LIMIT 3
#define MI_READ_TIMEOUT 1000

static const char init_vibration[3] = {0x20, 0x00, 0x00};
static const DWORD dpad_map[8] =
{
    MI_BUTTON_UP,
    MI_BUTTON_UP | MI_BUTTON_RIGHT,
    MI_BUTTON_RIGHT,
    MI_BUTTON_RIGHT | MI_BUTTON_DOWN,
    MI_BUTTON_DOWN,
    MI_BUTTON_DOWN | MI_BUTTON_LEFT,
    MI_BUTTON_LEFT,
    MI_BUTTON_LEFT | MI_BUTTON_UP
};

struct mi_gamepad;

struct mi_gamepad
{
    struct hid_device *device;
    struct mi_state state;
    void (*upd_cb)(struct hid_device *, struct mi_state *);
    void (*stop_cb)(struct hid_device *, BYTE);

    BOOL active;

    HANDLE hthread;

    struct mi_gamepad *prev;
    struct mi_gamepad *next;
};

static struct mi_gamepad *root_gp = NULL;
static SRWLOCK gp_lock = SRWLOCK_INIT;

static DWORD WINAPI _mi_gamepad_thread_proc(LPVOID lparam)
{
    struct mi_gamepad *gp = (struct mi_gamepad *)lparam;
    INT bytes_read = 0;
    BYTE break_reason = MI_BREAK_REASON_UNKNOWN;

    while (TRUE)
    {
        if (!gp->active)
        {
            break_reason = MI_BREAK_REASON_UNPLUGGED;
            break;
        }

        int read_attempt_count = 0;
        while (read_attempt_count++ < MI_READ_ATTEMPT_LIMIT)
        {
            bytes_read = hid_get_input_report(gp->device, MI_READ_TIMEOUT);
            if (bytes_read != 0)
            {
                break;
            }
        }

        if (bytes_read < 0)
        {
            break_reason = MI_BREAK_REASON_READ_ERROR;
            break;
        }

        gp->state.buttons = MI_BUTTON_NONE;

        gp->state.buttons |= (gp->device->input_buffer[0] & (1 << 0)) != 0 ? MI_BUTTON_A : 0;
        gp->state.buttons |= (gp->device->input_buffer[0] & (1 << 1)) != 0 ? MI_BUTTON_B : 0;
        gp->state.buttons |= (gp->device->input_buffer[0] & (1 << 3)) != 0 ? MI_BUTTON_X : 0;
        gp->state.buttons |= (gp->device->input_buffer[0] & (1 << 4)) != 0 ? MI_BUTTON_Y : 0;
        gp->state.buttons |= (gp->device->input_buffer[0] & (1 << 6)) != 0 ? MI_BUTTON_L1 : 0;
        gp->state.buttons |= (gp->device->input_buffer[0] & (1 << 7)) != 0 ? MI_BUTTON_R1 : 0;

        gp->state.buttons |= (gp->device->input_buffer[1] & (1 << 2)) != 0 ? MI_BUTTON_RETURN : 0;
        gp->state.buttons |= (gp->device->input_buffer[1] & (1 << 3)) != 0 ? MI_BUTTON_MENU : 0;
        gp->state.buttons |= (gp->device->input_buffer[1] & (1 << 5)) != 0 ? MI_BUTTON_LS : 0;
        gp->state.buttons |= (gp->device->input_buffer[1] & (1 << 6)) != 0 ? MI_BUTTON_RS : 0;

        gp->state.buttons |= gp->device->input_buffer[3] < 8 ? dpad_map[gp->device->input_buffer[3]] : 0;

        gp->state.left_stick_x = gp->device->input_buffer[4];
        gp->state.left_stick_y = gp->device->input_buffer[5];
        gp->state.right_stick_x = gp->device->input_buffer[6];
        gp->state.right_stick_y = gp->device->input_buffer[7];

        gp->state.l2_trigger = gp->device->input_buffer[10];
        gp->state.r2_trigger = gp->device->input_buffer[11];

        gp->state.accel_x = *(WORD *)(gp->device->input_buffer + 12);
        gp->state.accel_y = *(WORD *)(gp->device->input_buffer + 14);
        gp->state.accel_z = *(WORD *)(gp->device->input_buffer + 16);

        gp->state.battery = gp->device->input_buffer[18];

        gp->state.buttons |= gp->device->input_buffer[19] > 0 ? MI_BUTTON_MI_BTN : 0;

        gp->upd_cb(gp->device, &gp->state);
    }

    AcquireSRWLockExclusive(&gp_lock);
    if (gp->prev == NULL)
    {
        root_gp = gp->next;
    }
    else
    {
        gp->prev->next = gp->next;
        if (gp->next != NULL)
        {
            gp->next->prev = gp->prev;
        }
    }
    ReleaseSRWLockExclusive(&gp_lock);

    CloseHandle(gp->hthread);
    gp->stop_cb(gp->device, break_reason);
    free(gp);

    return 0;
}

void mi_gamepad_start(struct hid_device *device, void (*upd_cb)(struct hid_device *, struct mi_state *),
                      void (*stop_cb)(struct hid_device *, BYTE))
{
    if (hid_send_feature_report(device, init_vibration, sizeof(init_vibration)) <= 0)
    {
        stop_cb(device, MI_BREAK_REASON_INIT_ERROR);
        return;
    }

    struct mi_gamepad *gp = (struct mi_gamepad *)malloc(sizeof(struct mi_gamepad));
    gp->device = device;
    gp->upd_cb = upd_cb;
    gp->stop_cb = stop_cb;
    gp->active = TRUE;
    gp->next = NULL;

    AcquireSRWLockExclusive(&gp_lock);

    if (root_gp == NULL)
    {
        root_gp = gp;
        gp->prev = NULL;
    }
    else
    {
        struct mi_gamepad *cur_gp = root_gp;
        while (cur_gp->next != NULL)
        {
            cur_gp = cur_gp->next;
        }
        cur_gp->next = gp;
        gp->prev = cur_gp;
    }

    gp->hthread = CreateThread(NULL, 0, _mi_gamepad_thread_proc, gp, 0, NULL);
    if (gp->hthread == NULL)
    {
        if (gp->prev == NULL)
        {
            root_gp = NULL;
        }
        else
        {
            gp->prev->next = NULL;
        }
        free(gp);
        ReleaseSRWLockExclusive(&gp_lock);
        stop_cb(device, MI_BREAK_REASON_INIT_ERROR);
        return;
    }

    ReleaseSRWLockExclusive(&gp_lock);
}

void mi_gamepad_stop(struct hid_device *device)
{
    AcquireSRWLockShared(&gp_lock);
    struct mi_gamepad *cur_gp = root_gp;
    while (cur_gp != NULL)
    {
        if (_tcscmp(device->path, cur_gp->device->path) == 0)
        {
            cur_gp->active = FALSE;
            break;
        }
        cur_gp = cur_gp->next;
    }
    ReleaseSRWLockShared(&gp_lock);
}
