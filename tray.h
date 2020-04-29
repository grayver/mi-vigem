#ifndef TRAY_H
#define TRAY_H

#include <wtypes.h>

struct tray_menu;

struct tray
{
    LPTSTR icon;
    struct tray_menu *menu;
};

struct tray_menu
{
    LPTSTR text;
    BOOLEAN disabled;
    BOOLEAN checked;

    void (*cb)(struct tray_menu *);
    void *context;

    struct tray_menu *submenu;
};

int tray_init(struct tray *tray);
int tray_loop(int blocking);
void tray_update(struct tray *tray);
void tray_exit();

#endif /* TRAY_H */
