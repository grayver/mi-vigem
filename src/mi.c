#include <tchar.h>
#include <windows.h>
#include <synchapi.h>

#include "mi.h"
#include "hid.h"

#pragma comment(lib, "kernel32.lib")

#define MI_READ_ATTEMPT_LIMIT 3
#define MI_READ_TIMEOUT 1000

#define MI_VIBRATION_HEADER         0
#define MI_VIBRATION_SMALL_MOTOR    1
#define MI_VIBRATION_BIG_MOTOR      2

static const BYTE init_vibration[3] = { 0x20, 0x00, 0x00 };
static const BYTE calibration[24] =
{
    0x22, 0xFF,
    0x00, 0x7f, 0x7f, 0xff, 0x00, 0x7f, 0x7f, 0xff,
    0x00, 0x7f, 0x7f, 0xff, 0x00, 0x7f, 0x7f, 0xff,
    0x00, 0x7f, 0xff,
    0x00, 0x7f, 0xff
};
static const BYTE enable_accel[3] = { 0x31, 0x01, 0x08 };
static const BYTE disable_accel[3] = { 0x31, 0x00, 0x00 };

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

    HANDLE in_thread;
    HANDLE out_thread;

    SRWLOCK vibr_lock;
    BYTE small_motor;
    BYTE big_motor;

    struct mi_gamepad *prev;
    struct mi_gamepad *next;
};

static struct mi_gamepad *root_gp = NULL;
static SRWLOCK gp_lock = SRWLOCK_INIT;

static DWORD WINAPI _mi_input_thread_proc(LPVOID lparam)
{
    struct mi_gamepad *gp = (struct mi_gamepad *)lparam;
    INT bytes_read = 0;
    BYTE break_reason = MI_BREAK_REASON_UNKNOWN;

    while (TRUE)
    {
        if (!gp->active)
        {
            break_reason = MI_BREAK_REASON_REQUESTED;
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

        if (bytes_read < 0 || bytes_read == 0 && read_attempt_count > MI_READ_ATTEMPT_LIMIT)
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

    gp->active = FALSE;
    WaitForSingleObject(gp->out_thread, INFINITE);

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

    CloseHandle(gp->in_thread);
    CloseHandle(gp->out_thread);
    gp->stop_cb(gp->device, break_reason);
    free(gp);

    return 0;
}

static DWORD WINAPI _mi_output_thread_proc(LPVOID lparam)
{
    struct mi_gamepad *gp = (struct mi_gamepad *)lparam;
    BYTE vibration[3];

    vibration[MI_VIBRATION_HEADER] = init_vibration[MI_VIBRATION_HEADER];
    vibration[MI_VIBRATION_SMALL_MOTOR] = gp->small_motor;
    vibration[MI_VIBRATION_BIG_MOTOR] = gp->big_motor;

    while (gp->active)
    {
        AcquireSRWLockShared(&gp->vibr_lock);
        if (gp->small_motor != vibration[MI_VIBRATION_SMALL_MOTOR] || gp->big_motor != vibration[MI_VIBRATION_BIG_MOTOR])
        {
            vibration[MI_VIBRATION_SMALL_MOTOR] = gp->small_motor;
            vibration[MI_VIBRATION_BIG_MOTOR] = gp->big_motor;
            ReleaseSRWLockShared(&gp->vibr_lock);
            hid_send_feature_report(gp->device, vibration, sizeof(vibration));
        }
        else
        {
            ReleaseSRWLockShared(&gp->vibr_lock);
        }
    }

    return 0;
}

BOOL mi_gamepad_start(struct hid_device *device, void (*upd_cb)(struct hid_device *, struct mi_state *),
                      void (*stop_cb)(struct hid_device *, BYTE))
{
    if (hid_send_feature_report(device, init_vibration, sizeof(init_vibration)) <= 0)
    {
        return FALSE;
    }

    struct mi_gamepad *gp = (struct mi_gamepad *)malloc(sizeof(struct mi_gamepad));
    gp->device = device;
    gp->upd_cb = upd_cb;
    gp->stop_cb = stop_cb;
    gp->active = TRUE;
    InitializeSRWLock(&gp->vibr_lock);
    gp->small_motor = init_vibration[MI_VIBRATION_SMALL_MOTOR];
    gp->big_motor = init_vibration[MI_VIBRATION_BIG_MOTOR];
    gp->next = NULL;

    SECURITY_ATTRIBUTES security = {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle = TRUE};
    gp->in_thread = CreateThread(&security, 0, _mi_input_thread_proc, gp, CREATE_SUSPENDED, NULL);
    gp->out_thread = CreateThread(&security, 0, _mi_output_thread_proc, gp, CREATE_SUSPENDED, NULL);
    if (gp->in_thread == NULL || gp->out_thread == NULL)
    {
        if (gp->in_thread != NULL)
        {
            CloseHandle(gp->in_thread);
        }
        if (gp->out_thread != NULL)
        {
            CloseHandle(gp->out_thread);
        }
        free(gp);
        return FALSE;
    }

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
    ReleaseSRWLockExclusive(&gp_lock);

    ResumeThread(gp->in_thread);
    ResumeThread(gp->out_thread);
    return TRUE;
}

void mi_gamepad_set_vibration(struct hid_device *device, BYTE small_motor, BYTE big_motor)
{
    AcquireSRWLockShared(&gp_lock);
    struct mi_gamepad *cur_gp = root_gp;
    while (cur_gp != NULL)
    {
        if (_tcscmp(device->path, cur_gp->device->path) == 0)
        {
            break;
        }
        cur_gp = cur_gp->next;
    }
    ReleaseSRWLockShared(&gp_lock);
    if (cur_gp != NULL)
    {
        AcquireSRWLockExclusive(&cur_gp->vibr_lock);
        cur_gp->small_motor = small_motor;
        cur_gp->big_motor = big_motor;
        ReleaseSRWLockExclusive(&cur_gp->vibr_lock);
    }
}

void mi_gamepad_calibrate(struct hid_device *device)
{
    hid_send_feature_report(device, calibration, sizeof(calibration));
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
