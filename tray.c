#include <tchar.h>
#include <windows.h>
#include <shellapi.h>

#include "tray.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")


#define WM_TRAY_CALLBACK_MESSAGE (WM_USER + 1)
#define WC_TRAY_CLASS_NAME TEXT("TRAY")
#define WC_TRAY_MUTEX_NAME TEXT("MI-VIGEM")
#define ID_TRAY_FIRST 1000

static WNDCLASSEX wc;
static NOTIFYICONDATA nid;
static HWND hwnd;
static HMENU hmenu = NULL;
static HANDLE hmutex;

static LRESULT CALLBACK _tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_TRAY_CALLBACK_MESSAGE:
        if (lparam == WM_LBUTTONUP || lparam == WM_RBUTTONUP)
        {
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(hwnd);
            WORD cmd = TrackPopupMenu(hmenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                      p.x, p.y, 0, hwnd, NULL);
            SendMessage(hwnd, WM_COMMAND, cmd, 0);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (wparam >= ID_TRAY_FIRST)
        {
            MENUITEMINFO item = {
                .cbSize = sizeof(MENUITEMINFO),
                .fMask = MIIM_ID | MIIM_DATA,
            };
            if (GetMenuItemInfo(hmenu, wparam, FALSE, &item))
            {
                struct tray_menu *menu = (struct tray_menu *)item.dwItemData;
                if (menu != NULL && menu->cb != NULL)
                {
                    menu->cb(menu);
                }
            }
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static HMENU _tray_menu(struct tray_menu *m, UINT *id)
{
    HMENU hmenu = CreatePopupMenu();
    for (; m != NULL && m->text != NULL; m++, (*id)++)
    {
        if (_tcscmp(m->text, TEXT("-")) == 0)
        {
            InsertMenu(hmenu, *id, MF_SEPARATOR, TRUE, TEXT(""));
        }
        else
        {
            MENUITEMINFO item;
            memset(&item, 0, sizeof(item));
            item.cbSize = sizeof(MENUITEMINFO);
            item.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE | MIIM_DATA;
            item.fType = 0;
            item.fState = 0;
            if (m->submenu != NULL)
            {
                item.fMask = item.fMask | MIIM_SUBMENU;
                item.hSubMenu = _tray_menu(m->submenu, id);
            }
            if (m->disabled)
            {
                item.fState |= MFS_DISABLED;
            }
            if (m->checked)
            {
                item.fState |= MFS_CHECKED;
            }
            item.wID = *id;
            item.dwTypeData = m->text;
            item.dwItemData = (ULONG_PTR)m;

            InsertMenuItem(hmenu, *id, TRUE, &item);
        }
    }
    return hmenu;
}

int tray_init(struct tray *tray)
{
    hmutex = CreateMutex(NULL, TRUE, WC_TRAY_MUTEX_NAME);
    if (hmutex == NULL)
    {
        return -1;
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = _tray_wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WC_TRAY_CLASS_NAME;
    if (!RegisterClassEx(&wc))
    {
        return -1;
    }

    hwnd = CreateWindowEx(0, WC_TRAY_CLASS_NAME, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (hwnd == NULL)
    {
        return -1;
    }
    UpdateWindow(hwnd);

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 0;
    nid.uFlags = NIF_ICON | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAY_CALLBACK_MESSAGE;
    Shell_NotifyIcon(NIM_ADD, &nid);

    tray_update(tray);
    return 0;
}

int tray_loop(int blocking)
{
    MSG msg;
    if (blocking)
    {
        GetMessage(&msg, NULL, 0, 0);
    }
    else
    {
        PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
    }
    if (msg.message == WM_QUIT)
    {
        return -1;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    return 0;
}

void tray_update(struct tray *tray)
{
    HMENU prevmenu = hmenu;
    UINT id = ID_TRAY_FIRST;
    hmenu = _tray_menu(tray->menu, &id);
    SendMessage(hwnd, WM_INITMENUPOPUP, (WPARAM)hmenu, 0);
    HICON icon;
    ExtractIconEx(tray->icon, 0, NULL, &icon, 1);
    if (nid.hIcon)
    {
        DestroyIcon(nid.hIcon);
    }
    nid.hIcon = icon;
    Shell_NotifyIcon(NIM_MODIFY, &nid);

    if (prevmenu != NULL)
    {
        DestroyMenu(prevmenu);
    }
}

void tray_exit()
{
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (nid.hIcon != 0)
    {
        DestroyIcon(nid.hIcon);
    }
    if (hmenu != 0)
    {
        DestroyMenu(hmenu);
    }
    PostQuitMessage(0);
    UnregisterClass(WC_TRAY_CLASS_NAME, GetModuleHandle(NULL));
    ReleaseMutex(hmutex);
    CloseHandle(hmutex);
}
