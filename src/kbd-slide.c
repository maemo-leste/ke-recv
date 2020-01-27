/**
  @file kbd-slide.c
  Code concerning state of the slide keyboard.

  This file is part of ke-recv.

  Copyright (C) 2020 Arthur Demchenkov <spinal.by@gmail.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  version 2 as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  02110-1301 USA
*/

#include <glib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libevdev/libevdev.h>

#include "events.h"
#include "kbd-slide.h"

/* Path to the input device directory */
#define DEV_INPUT_PATH "/dev/input"
/* Prefix for event files */
#define EVENT_FILE_PREFIX "event"
/* The name of device we are going to monitor */
#define KBD_SLIDE_DEV_NAME "gpio_keys"

static struct libevdev *kbd_slide_dev = NULL;
static GIOChannel *kbd_slide_iochan = NULL;
static guint kbd_slide_src_id = 0;

static gboolean kbd_slide_add_handler(void);

/**
 * Try to set input device as a keypad slide handler
 * @param path  the device file path to try
 * @return  TRUE if device is suitable, FALSE otherwise
 */
static gboolean
kbd_slide_set_input_dev(gchar *path)
{
    gint fd, rc;
    struct libevdev *dev = NULL;

    fd = open(path, O_RDONLY|O_NONBLOCK);
    if (fd < 0)
        return FALSE;

    rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        close(fd);
        return FALSE;
    }

    if (strcmp(libevdev_get_name(dev), KBD_SLIDE_DEV_NAME) == 0 &&
        libevdev_has_event_type(dev, EV_SW))
    {
        gboolean state = libevdev_get_event_value(dev, EV_SW, SW_KEYPAD_SLIDE);
        inform_slide_keyboard(state);
        kbd_slide_dev = dev;
        return TRUE;
    }

    libevdev_free(dev);
    close(fd);
    return FALSE;
}

/**
 * Find and set proper input device for slide keypad monitoring
 * @return  TRUE if found, FALSE otherwise
 */
static gboolean
kbd_slide_find_evdev()
{
    DIR *dir = NULL;
    struct dirent *direntry = NULL;
    gboolean result = FALSE;

    if ((dir = opendir(DEV_INPUT_PATH)) == NULL) {
        perror(__func__);
        return FALSE;
    }

    for (direntry = readdir(dir);
         (direntry != NULL && telldir(dir));
         direntry = readdir(dir))
    {
        gchar *dev_path = NULL;
        if (strncmp(direntry->d_name, EVENT_FILE_PREFIX,
                    strlen(EVENT_FILE_PREFIX)) != 0)
        {
            continue;
        }

        dev_path = g_strconcat(DEV_INPUT_PATH, "/", direntry->d_name, NULL);

        if (kbd_slide_set_input_dev(dev_path)) {
            result = kbd_slide_add_handler();
            g_free(dev_path);
            break;
        }

        g_free(dev_path);
    }

    /* Report, but ignore, errors when closing directory */
    if (closedir(dir) == -1)
        perror(__func__);

    return result;
}

/**
 * Keypad slide state changes handler
 */
static gboolean
kbd_slide_handler(GIOChannel *src, GIOCondition cond, gpointer data)
{
    struct input_event ev;
    int rc;

    for (;;) {
        rc = libevdev_next_event(kbd_slide_dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            /* We are out of sync, fix it */
            do {
                rc = libevdev_next_event(kbd_slide_dev,
                                         LIBEVDEV_READ_FLAG_SYNC, &ev);
            } while (rc == LIBEVDEV_READ_STATUS_SYNC);

            /* Re-read current keyboard slide state */
            gboolean state = libevdev_get_event_value(kbd_slide_dev,
                                                      EV_SW, SW_KEYPAD_SLIDE);
            inform_slide_keyboard(state);
            continue;
        }

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (libevdev_event_is_code(&ev, EV_SW, SW_KEYPAD_SLIDE)) {
                inform_slide_keyboard(ev.value);
            }
            continue;
        }

        if (rc == -EAGAIN) {
            /* There's no more input data */
            return TRUE;
        }

        /* An error occured or we are out of sync, stop handling */
        return FALSE;
    }
}

static gboolean
kbd_slide_add_handler()
{
    kbd_slide_iochan = g_io_channel_unix_new(libevdev_get_fd(kbd_slide_dev));
    if (kbd_slide_iochan == NULL)
        return FALSE;

    kbd_slide_src_id = g_io_add_watch(kbd_slide_iochan, G_IO_IN,
                                      kbd_slide_handler, NULL);
    return TRUE;
}

void
kbd_slide_monitor_start()
{
    if (kbd_slide_dev != NULL) {
        /* Already started */
        return;
    }

    if (!kbd_slide_find_evdev()) {
        /* Keypad slide state is considered closed if the device is not found */
        inform_slide_keyboard(FALSE);
        kbd_slide_monitor_stop();
    }
}

void
kbd_slide_monitor_stop()
{
    if (kbd_slide_src_id != 0) {
        g_source_remove(kbd_slide_src_id);
        kbd_slide_src_id = 0;
    }

    if (kbd_slide_iochan != NULL) {
        g_io_channel_unref(kbd_slide_iochan);
        kbd_slide_iochan = NULL;
    }

    if (kbd_slide_dev != NULL) {
        gint fd = libevdev_get_fd(kbd_slide_dev);
        if (fd != -1)
            close(fd);
        libevdev_free(kbd_slide_dev);
        kbd_slide_dev = NULL;
    }
}
