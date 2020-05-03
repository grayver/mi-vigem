#include <stdio.h>
#include <string.h>
#include <tchar.h>

#include "tray.h"

#define TRAY_ICON1 TEXT("APP_ICON")
#define TRAY_ICON2 TEXT("APP_ICON")

static const GUID hid_class = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

static struct tray tray;

static void toggle_cb(struct tray_menu *item)
{
    printf("toggle cb\n");
    item->checked = !item->checked;
    tray_update(&tray);
}

static void hello_cb(struct tray_menu *item)
{
    (void)item;
    printf("hello cb\n");
    if (_tcscmp(tray.icon, TRAY_ICON1) == 0)
    {
        tray.icon = TRAY_ICON2;
    }
    else
    {
        tray.icon = TRAY_ICON1;
    }
    tray_update(&tray);
}

static void quit_cb(struct tray_menu *item)
{
    (void)item;
    printf("quit cb\n");
    tray_exit();
}

static void submenu_cb(struct tray_menu *item)
{
    (void)item;
    printf("submenu: clicked on %s\n", item->text);
    tray_update(&tray);
}

static void device_cb(UINT op, LPTSTR path)
{
    printf("device operation %d with path %s\n", op, path);
}

// Test tray init
static struct tray tray = {
    .icon = TRAY_ICON1,
    .tip = TEXT("Mi-ViGEm"),
    .menu =
        (struct tray_menu[]){
            {.text = "Hello", .cb = hello_cb},
            {.text = "Checked", .checked = 1, .cb = toggle_cb},
            {.text = "Disabled", .disabled = 1},
            {.text = "-"},
            {.text = "SubMenu",
             .submenu =
                 (struct tray_menu[]){
                     {.text = "FIRST", .checked = 1, .cb = submenu_cb},
                     {.text = "SECOND",
                      .submenu =
                          (struct tray_menu[]){
                              {.text = "THIRD",
                               .submenu =
                                   (struct tray_menu[]){
                                       {.text = "7", .cb = submenu_cb},
                                       {.text = "-"},
                                       {.text = "8", .cb = submenu_cb},
                                       {.text = NULL}}},
                              {.text = "FOUR",
                               .submenu =
                                   (struct tray_menu[]){
                                       {.text = "5", .cb = submenu_cb},
                                       {.text = "6", .cb = submenu_cb},
                                       {.text = NULL}}},
                              {.text = NULL}}},
                     {.text = NULL}}},
            {.text = "-"},
            {.text = "Quit", .cb = quit_cb},
            {.text = NULL}},
};

int main()
{
    if (tray_init(&tray) < 0)
    {
        printf("failed to create tray\n");
        return 1;
    }
    tray_register_device_notification(hid_class, &device_cb);
    while (tray_loop(TRUE) == 0)
    {
        printf("iteration\n");
    }
    return 0;
}
