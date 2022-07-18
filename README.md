# Mi-ViGEm

XBox360 controller emulation for Xiaomi gamepad. Supports multiple devices and vibration. Implemented on vanilla C and WinAPI, supposed to be much more lightweight comparing to it's analogs.
XBox360 emulation driver is provided by ViGEm (https://github.com/ViGEm/ViGEmBus), by Benjamin Höglinger.

## Requirements
- Windows 10 (should work on Windows 7 and 8 also)
- ViGEm bus installed (can be downloaded [here](https://github.com/ViGEm/ViGEmBus/releases))

## How it works
Mi-ViGEm program at start scans for Xiaomi Gamepad devices and then proxies found Xiaomi gamepads to virtual XBox360 gamepads (with help of ViGEmBus). Also Mi-ViGEm subscribes to system device plug/unplug notifications and rescan devices on each notification.
All found devices are displayed in tray icon context menu (along with their battery level). Manual device rescan can be initiated via tray icon context menu.

## Double input problem
Mi-ViGEm doesn't replace Xiaomi gamepad with XBox360 gamepad, it just creates virtual XBox360 gamepad in parallel, so both gamepads are presented in OS. This causes problems in some games, which read input from both real and virtual gamepads. There is nothing to do with that on user application level, only kernel level driver can help. Fortunately, ViGEm developed HidHide driver which deals with that problem (can be downloaded [here](https://github.com/ViGEm/HidHide/releases)).

Mi-ViGEm provides integration with HidHide driver since version 1.2. It automatically detects HidHide presence and makes necessary changes in white and black lists. You only need to install HidHide (reboot is required) and run Mi-ViGEm.

## Thanks to

This project is inspired by following projects written on C#:
- https://github.com/irungentoo/Xiaomi_gamepad
- https://github.com/dancol90/mi-360

Thanks to following libraries and resources:
- https://github.com/libusb/hidapi for HID implementation
- https://github.com/zserge/tray for lightweight tray app implementation
- https://www.flaticon.com/authors/freepik for application icon
