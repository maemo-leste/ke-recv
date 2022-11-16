#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#include <gudev/gudev.h>

#include "udev-helper.h"


/*
 * Code to detect changes in usb port state using udev.
 *
 * Ideally it finds a usb supply node, as well as udc driver.
 *
 * Generally it is expected that a gadget is always loaded, otherwise
 * pc-detection doesn't work.
 *
 * If any gadget is active, state should change to *_peripheral (on n900) and
 * vbus is on. If a dumb charger is connected, then state will be *_idle.
 */

typedef struct {
    gboolean present;
    const gchar* subsystem;
    const gchar* driver;
    const gchar* name;
} DeviceDriver;

typedef struct {
    const gchar* name;
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
    gint usb_mode;
    gint supply_mode;
} PrivData;

static gpointer user_data = NULL;
static UhCallback user_callback = NULL;

static GUdevClient* client = NULL;
static GUdevDevice* supply = NULL;
static DeviceDrivers *active_device;
static guint update_info_timeout = 0;
/*
  list of UDC devices - so far we do not support more than one, but,
  in future, that may change
 */
GList *udc_devices = NULL;
/* list of extcon devices that support USB cable */
GList *extcon_devices = NULL;

static PrivData cache = { .usb_mode = USB_MODE_UNKNOWN,
                          .supply_mode = USB_SUPPLY_UNKNOWN };

static DeviceDrivers drivers[] = {
    {
        .name = "Nokia N900",
        .supply_driver = {
            .present = TRUE,
            .subsystem = "power_supply",
            .driver = NULL,
            .name = "isp1704",
        },
    },
    {
        .name = "Motorola Droid 4",
        /* We can't read the mode from extcon, but at least we can use this to
         * trigger on cable plugs and report on musb modes */
        .supply_driver = {
            .present = TRUE,
            .subsystem = "power_supply",
            .driver = NULL,
            .name = "usb",
        },
    },
};
/* TODO: Also add generic best-effort later on, perhaps with attr matching? */


static gint read_usb_mode(void) {
    const gchar* path = udc_devices->data;
    gchar *strmode = NULL;
    int mode;

    mode = USB_MODE_UNKNOWN;

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

    return mode;
}

static gchar **extcon_get_state(const char *path) {
    gchar **rv = NULL;
    gchar *fname = g_strconcat(path, "/state", NULL);
    GError *error = NULL;
    gchar *state;

    if (g_file_get_contents(fname, &state, NULL, &error)) {
        rv = g_strsplit_set(state, "=\n", 0);
        g_free(state);
    } else {
        fprintf(stderr, "error reading %s: %s\n", fname, error->message);
        g_clear_error(&error);
    }

    g_free(fname);

    return rv;
}

static gint read_supply_mode(void) {
    int mode;

    mode = USB_SUPPLY_UNKNOWN;

    if (supply) {
        const gchar* supply_sysfs_path = g_udev_device_get_sysfs_path(supply);
        gchar* path;
        gchar *strmode = NULL;

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
    } else {
        gchar **state = extcon_get_state(extcon_devices->data);

        if (state) {
            gchar **p = state;

            while (*p) {
                if (!p[1] || !*p[1])
                    break;
                if (!strcmp(*p, "DCP") && *p[1] != '0') {
                    mode = USB_SUPPLY_DCP;
                    break;
                } else if (!strcmp(*p, "CDP") && *p[1] != '0') {
                    mode = USB_SUPPLY_CDP;
                    break;
                } else if (!strcmp(*p, "USB") && *p[1] != '0') {
                    /* do not overwrite high-priority modes */
                    if (mode == USB_SUPPLY_UNKNOWN)
                        mode = USB_SUPPLY_NONE;
                }

                p += 2;
            }

            g_strfreev(state);
        }
    }

    return mode;
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
    if (res)
        g_object_ref(res);

    g_list_free_full(l, g_object_unref);

    return res;
}

static int setup_udev(void) {
    const gchar *subsystems[] = { NULL};
    client = g_udev_client_new(subsystems);
    if (!client)
        return 1;

    return 0;
}

static gboolean update_info() {
    update_info_timeout = 0;
    cache.supply_mode = read_supply_mode();
    cache.usb_mode = read_usb_mode();
    fprintf(stderr, "usb_mode: %d; supply_mode: %d\n", cache.usb_mode, cache.supply_mode);

    if ((user_callback)) {
        user_callback(cache.usb_mode, cache.supply_mode, user_data);
    }

    return G_SOURCE_REMOVE;
}

static void idle_update_info() {
    if (update_info_timeout)
        g_source_remove(update_info_timeout);

    /* Not all values update fast enough, and we're not in a hurry */
    update_info_timeout = g_timeout_add(1000, update_info, NULL);
}

static int find_devices(void) {
    GList *l, *devices;
    gboolean ok = FALSE;

    g_list_free_full(udc_devices, g_free);
    udc_devices = NULL;

    g_list_free_full(extcon_devices, g_free);
    extcon_devices = NULL;

    devices = g_udev_client_query_by_subsystem(client, "udc");

    for (l = devices; l; l = l->next) {
        const char *s = g_udev_device_get_sysfs_path(l->data);
        gchar *sysfs_path;

        fprintf(stderr, "Found udc %s\n", s);
        sysfs_path = g_strconcat(s, "/device/mode", NULL);
        udc_devices = g_list_append(udc_devices, sysfs_path);
    }

    if (!devices) {
        fprintf(stderr, "No udc device found\n");
        return 1;
    }

    g_list_free_full(devices, g_object_unref);

    devices = g_udev_client_query_by_subsystem(client, "extcon");

    for (l = devices; l; l = l->next) {
        const char *s = g_udev_device_get_sysfs_path(l->data);
        gchar **state = extcon_get_state(s);

        if (state) {
            if (g_strv_contains((const char *const*)state, "USB")) {
                fprintf(stderr, "Found USB extcon %s\n", s);
                extcon_devices = g_list_append(extcon_devices, g_strdup(s));
            }

            g_strfreev(state);
        }
    }

    g_list_free_full(devices, g_object_unref);

    if (!extcon_devices)
    {
        int i;
        DeviceDrivers *d;
        for (i = 0; i < G_N_ELEMENTS(drivers); i++) {
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

            active_device = d;
            ok = TRUE;
            break;
        }
    } else {
        ok = TRUE;
    }

    if (ok) {
        return 0;
    }

    return 1;
}

static gint find_extcon(gconstpointer a, gconstpointer b) {
    return strcmp(a, b);
}

static void on_uevent(GUdevClient *client, const char *action, GUdevDevice *device) {
    (void)client;
    (void)action;

    if (!strcmp(action, "add") || !strcmp(action, "remove")) {
        const gchar* subsys = g_udev_device_get_subsystem(device);

        if (!strcmp(subsys, "udc") || !strcmp(subsys, "extcon") ||
            !strcmp(subsys, "power_supply")) {
            find_devices();
        }
    } else {
        const gchar* sysfs_path = g_udev_device_get_sysfs_path(device);
        const gchar* supply_sysfs_path;

        if (g_list_find_custom(extcon_devices, sysfs_path, find_extcon)) {
            idle_update_info();
        } else if (active_device) {
            if (active_device->supply_driver.present)
                supply_sysfs_path = g_udev_device_get_sysfs_path(supply);

            if (active_device->supply_driver.present && (strcmp(supply_sysfs_path, sysfs_path) == 0)) {
                idle_update_info();
            }
        }
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

void uh_query_state(gint* usb_mode, gint* supply_mode) {
    *supply_mode = read_supply_mode();
    *usb_mode = read_usb_mode();
}

/* TODO */
char *uh_get_device_name() {
    return NULL;
}

#if 0
/* TODO: g_udev_device_has_sysfs_attr can be used to see if something has attrs
 * like vbus and mode */

static gboolean pc_connected() {
    return (cache.usb_mode == USB_MODE_B_PERIPHERAL) || (cache.usb_mode == USB_MODE_A_PERIPHERAL);
}

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
