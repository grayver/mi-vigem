# Mi-ViGEm

XBox360 controller emulation for Xiaomi gamepad. Supports multiple devices and vibration. Implemented on vanilla C and WinAPI, supposed to be much more lightweight comparing to it's analogs.
XBox360 emulation driver is provided by ViGEm (https://github.com/ViGEm/ViGEmBus), by Benjamin Höglinger.

## Requirements
- Windows 7 or newer
- Microsoft Visual C++ 2019 redistributable (can be downloaded [here](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads))
- ViGEm bus installed (can be downloaded [here](https://github.com/ViGEm/ViGEmBus/releases))

## How it works
Mi-ViGEm program at start scans for Xiaomi Gamepad devices and then proxies found Xiaomi gamepads to virtual XBox360 gamepads (with help of ViGEmBus). Also Mi-ViGEm subscribes to system device plug/unplug notifications and rescan devices on each notification.
All found devices are displayed in tray icon context menu (along with their battery level). Manual device rescan can be initiated via tray icon context menu.

## Thanks to

This project is inspired by following projects written on C#:
- https://github.com/irungentoo/Xiaomi_gamepad
- https://github.com/dancol90/mi-360

Thanks to following libraries and resources:
- https://github.com/libusb/hidapi for HID implementation
- https://github.com/zserge/tray for lightweight tray app implementation
- https://www.flaticon.com/authors/freepik for application icon
