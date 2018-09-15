#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <gudev/gudev.h>

#include "udev-helper.h"


/*
 * Code to detect changes in usb port state using udev.
 *
 * Ideally it finds a usb supply node, as well as udc driver and usb phy -- this
 * is the case on the N900.
 *
 * If there is no usb supply node, then we have to guess if a cable is
 * connected based upon usb phy vbus and udc driver mode. If any gadget is
 * active, state should change to *_peripheral (on n900) and vbus is on. If a
 * dumb charger is connected, then state will be *_idle.
 */

typedef struct {
    gboolean present;
    const gchar* subsystem;
    const gchar* driver;
    const gchar* name;
} DeviceDriver;

typedef struct {
    const gchar* name;
    const DeviceDriver phy_driver;
    const DeviceDriver usb_driver;
    const DeviceDriver supply_driver;
} DeviceDrivers;

/*
 *
 * TODO:
 * - Implement uh_get_device_name (add to cache?)
 * - Rework usb_plugin.c to use libusbgx [1]
 * - Test on device with proper otg detection (droid4, lime2) and add/implement
 *   USB_MODE_*
 * - Figure out how to free `client`
 * - Run through valgrind
 * - Fix naming of 'otg driver' -- should be UDC I think...
 *
 * [1] https://github.com/libusbgx/libusbgx
 */

typedef struct {
    gboolean vbus;
    gint usb_mode;
    gint supply_mode;
} PrivData;

static gpointer user_data = NULL;
static UhCallback user_callback = NULL;

static GUdevClient* client = NULL;
static GUdevDevice* phy = NULL;
static GUdevDevice* otg = NULL;
static GUdevDevice* supply = NULL;
static DeviceDrivers *active_device;

static PrivData cache = { .vbus = 0,
                          .usb_mode = USB_MODE_UNKNOWN,
                          .supply_mode = USB_SUPPLY_UNKNOWN };

#define DRIVER_COUNT 1
static DeviceDrivers drivers[DRIVER_COUNT] = {
    {
        .name = "Nokia N900",
        .phy_driver = {
            .present = TRUE,
            .subsystem = "platform",
            .driver = "twl4030_usb",
            .name = NULL,
        },
        .usb_driver = {
            .present = TRUE,
            .subsystem = "platform",
            .driver = "musb-hdrc",
            .name = NULL,
        },
        .supply_driver = {
            .present = TRUE,
            .subsystem = "power_supply",
            .driver = NULL,
            .name = "isp1704",
        },
    },
#if 0
    {
        .name = "Motorola Droid 4",
        .phy_driver = {
            .present = TRUE,
            .subsystem = NULL,
            .driver = NULL,
            .name = NULL,
        },
        .usb_driver = {
            .present = TRUE,
            .subsystem = NULL,
            .driver = NULL,
            .name = NULL,
        },
        .supply_driver = {
            .present = FALSE,
            .subsystem = NULL,
            .driver = NULL,
            .name = NULL,
        },
    },
#endif
};
/* TODO: Also add generic best-effort later on, perhaps with attr matching? */


static gint read_usb_mode(void) {
    const gchar* otg_sysfs_path = g_udev_device_get_sysfs_path(otg);
    gchar* path;
    gchar *strmode = NULL;
    int mode;

    mode = USB_MODE_UNKNOWN;

    path = g_strconcat(otg_sysfs_path, "/mode", NULL);
    gboolean ok = g_file_get_contents(path, &strmode, NULL, NULL);
    if (ok) {
        if (strcmp(strmode, "b_idle\n") == 0) {
            mode = USB_MODE_B_IDLE;
        } else if (strcmp(strmode, "b_peripheral\n") == 0) {
            mode = USB_MODE_B_PERIPHERAL;
        } else if (strcmp(strmode, "b_host\n") == 0) {
            mode = USB_MODE_B_HOST;
        } else if (strcmp(strmode, "a_idle\n") == 0) {
            mode = USB_MODE_A_IDLE;
        } else if (strcmp(strmode, "a_peripheral\n") == 0) {
            mode = USB_MODE_A_PERIPHERAL;
        } else if (strcmp(strmode, "a_host\n") == 0) {
            mode = USB_MODE_A_HOST;
        }

        free(strmode);
    }
    free(path);

    return mode;
}

static gint read_supply_mode(void) {
    const gchar* supply_sysfs_path = g_udev_device_get_sysfs_path(supply);
    gchar* path;
    gchar *strmode = NULL;
    int mode;

    mode = USB_SUPPLY_UNKNOWN;

    path = g_strconcat(supply_sysfs_path, "/type", NULL);
    gboolean ok = g_file_get_contents(path, &strmode, NULL, NULL);
    if (ok) {
        if (strcmp(strmode, "USB_CDP\n") == 0) {
            mode = USB_SUPPLY_CDP;
        } else if (strcmp(strmode, "USB_DCP\n") == 0) {
            mode = USB_SUPPLY_DCP;
        } else if (strcmp(strmode, "USB\n") == 0) {
            mode = USB_SUPPLY_NONE;
        }

        free(strmode);
    }
    free(path);

    return mode;
}

static gint read_phy_vbus(void) {
    const gchar* phy_sysfs_path = g_udev_device_get_sysfs_path(phy);
    gchar* path;
    gchar *strmode = NULL;
    int vbus;

    vbus = 0;

    path = g_strconcat(phy_sysfs_path, "/vbus", NULL);
    gboolean ok = g_file_get_contents(path, &strmode, NULL, NULL);
    if (ok) {
        if (strcmp(strmode, "on\n") == 0) {
            vbus = 1;
        } else if (strcmp(strmode, "off\n") == 0) {
            vbus = 0;
        }

        free(strmode);
    }
    free(path);

    return vbus;
}

static GUdevDevice* find_device(const gchar* subsystem_match, const gchar* driver_match, const gchar* name_match) {
    GList *l, *e;
    GUdevDevice *dev, *res = NULL;

    l = g_udev_client_query_by_subsystem(client, subsystem_match);
    for (e = l; e; e = e->next) {
        const gchar* driver;
        const gchar* name;

        dev = (GUdevDevice*)e->data;
        driver = g_udev_device_get_driver(dev);
        name = g_udev_device_get_name(dev);

        if (driver && name && driver_match && name_match) {
            if ((strcmp(driver, driver_match) == 0) &&
                (strcmp(name, name_match) == 0)) {
                res = dev;
                goto DONE;
            }
        } else if (driver && driver_match && (strcmp(driver, driver_match) == 0)) {
            res = dev;
            goto DONE;
        } else if (name && name_match && (strcmp(name, name_match) == 0)) {
            res = dev;
            goto DONE;
        }
    }

DONE:
    g_list_free(l);

    return res;
}

static int setup_udev(void) {
    const gchar *subsystems[] = { NULL};
    client = g_udev_client_new(subsystems);
    if (!client)
        return 1;

    return 0;
}

static int find_devices(void) {
    int i;
    DeviceDrivers *d;
    gboolean ok = FALSE;

    for (i = 0; i < DRIVER_COUNT; i++) {
        phy = NULL;
        otg = NULL;
        supply = NULL;

        d = &drivers[i];
        fprintf(stderr, "Probing for drivers for %s\n", d->name);

        if (d->supply_driver.present == TRUE) {
            supply = find_device(d->supply_driver.subsystem,
                                 d->supply_driver.driver,
                                 d->supply_driver.name);
            if (!supply) {
                fprintf(stderr, "Cannot find supply for %s\n", d->name);
                continue;
            }
            fprintf(stderr, "Found supply\n");
        }

        if (d->phy_driver.present == TRUE) {
            phy = find_device(d->phy_driver.subsystem,
                              d->phy_driver.driver,
                              d->phy_driver.name);
            if (!phy) {
                fprintf(stderr, "Cannot find phy for %s\n", d->name);
                continue;
            }
            fprintf(stderr, "Found phy\n");
        }

        if (d->usb_driver.present == TRUE) {
            otg = find_device(d->usb_driver.subsystem,
                              d->usb_driver.driver,
                              d->usb_driver.name);
            if (!otg) {
                fprintf(stderr, "Cannot find otg for %s\n", d->name);
                continue;
            }
            fprintf(stderr, "Found otg\n");
        }

        active_device = d;
        ok = TRUE;
        break;
    }

    if (ok) {
        cache.vbus = read_phy_vbus();
        cache.usb_mode = read_usb_mode();
        if (active_device->supply_driver.present)
            cache.supply_mode = read_supply_mode();
        return 0;
    }

    return 1;
}

static gboolean pc_connected() {
    if (active_device->supply_driver.present)
        return cache.supply_mode == USB_SUPPLY_CDP;
    else
        return cache.vbus && ((cache.usb_mode == USB_MODE_B_PERIPHERAL) || (cache.usb_mode == USB_MODE_A_PERIPHERAL));
}

static gboolean update_info() {
    cache.supply_mode = read_supply_mode();
    cache.usb_mode = read_usb_mode();
    cache.vbus = read_phy_vbus();
    fprintf(stderr, "VBUS: %d, usb_mode: %d; supply_mode: %d\n", cache.vbus, cache.usb_mode, cache.supply_mode);

    if ((user_callback)) {
        user_callback(cache.vbus, cache.usb_mode, cache.supply_mode, user_data);
    }

    return G_SOURCE_REMOVE;
}

static void on_uevent(GUdevClient *client, const char *action, GUdevDevice *device) {
    (void)client;
    (void)action;

    const gchar* sysfs_path = g_udev_device_get_sysfs_path(device);
    const gchar* phy_sysfs_path;
    const gchar* supply_sysfs_path;

    phy_sysfs_path = g_udev_device_get_sysfs_path(phy);

    if (active_device->supply_driver.present)
        supply_sysfs_path = g_udev_device_get_sysfs_path(supply);

    if ((active_device->supply_driver.present && (strcmp(supply_sysfs_path, sysfs_path) == 0)) ||
       ((!active_device->supply_driver.present) && (strcmp(phy_sysfs_path, sysfs_path) == 0))) {
        /* Not all values update fast enough, and we're not in a hurry */
        g_timeout_add(1000, update_info, NULL);
    }
    return;
}

int uh_init() {
    int ret = 1;
    client = NULL;
    user_data = NULL;
    user_callback = NULL;

    ret = setup_udev();
    if (ret) {
        fprintf(stderr, "setup_udev failed\n");
        return ret;
    }
    ret = find_devices();
    if (ret) {
        fprintf(stderr, "find_devices failed\n");
        return ret;
    }

    g_signal_connect(client, "uevent", G_CALLBACK(on_uevent), NULL);

    return ret;
}

int uh_destroy() {
    if (client) {
        /* TODO: Figure out how to free - just unref ? */
    }

    return 0;
}

void uh_set_callback(UhCallback cb, gpointer data) {
    user_data = data;
    user_callback = cb;

    return;
}

void uh_query_state(gboolean* vbus, gint* usb_mode, gint* supply_mode) {
    *supply_mode = read_supply_mode();
    *usb_mode = read_usb_mode();
    *vbus = read_phy_vbus();
}

/* TODO */
char *uh_get_device_name() {
    return NULL;
}

#if 0
/* TODO: g_udev_device_has_sysfs_attr can be used to see if something has attrs
 * like vbus and mode */

static void test_callback(gboolean pc_connected, gpointer data) {
    fprintf(stderr, "test_callback: PC connected: %d - %p.\n", pc_connected, data);
}

static int main_loop(void) {
    static GMainLoop *loop = NULL;

    uh_init();
    uh_set_callback((UhCallback)test_callback, (void*)42);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    uh_destroy();

    return 0;
}

int
main (int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    return main_loop();
}
#endif
