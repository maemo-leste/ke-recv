/**
  @file ke-recv.c

  This file is part of ke-recv.

  Copyright (C) 2004-2008 Nokia Corporation. All rights reserved.

  Author: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>

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

#include <libhal.h>

#include "ke-recv.h"
#include "exec-func.h"
#include "gui.h"
#include "events.h"
#include "camera.h"
#include "service-launcher.h"
#include <hildon-mime.h>
#include <libgen.h>

#define FDO_SERVICE "org.freedesktop.Notifications"
#define FDO_OBJECT_PATH "/org/freedesktop/Notifications"
#define FDO_INTERFACE "org.freedesktop.Notifications"
#define USB_DOMAIN "hildon-status-bar-usb"
#define DEFAULT_USB_CABLE_UDI \
        "/org/freedesktop/Hal/devices/usb_device_0_0_musb_hdrc"
#define DESKTOP_SVC "com.nokia.hildon-desktop"

extern GConfClient* gconfclient;

const char* camera_out_udi = NULL;
const char* camera_turned_udi = NULL;
const char* slide_keyboard_udi = NULL;
const char* usb_cable_udi = NULL;

static char *usb_device_name = NULL;
static char *default_usb_device_name = NULL;

/* memory card information structs */
static mmc_info_t int_mmc, ext_mmc;
static gboolean int_mmc_enabled = FALSE;
static gboolean mmc_initialised = FALSE;

/* list of USB mass storages */
static storage_info_t *storage_list = NULL;

static usb_state_t usb_state = S_INVALID_USB_STATE;
static guint usb_pending_timer_id = 0;
static guint usb_mounting_timer_id = 0;
static dbus_uint32_t usb_dialog = -1;
static int usb_unmount_timeout = 0;
extern gboolean device_locked;
gboolean desktop_started = FALSE;
static gboolean delayed_auto_install_check = FALSE;
static gboolean mmc_keys_set = FALSE;

/* "the connection" is the connection where "the message" (i.e.
   the current message or signal that we're handling) came */
static DBusConnection* the_connection = NULL;
static DBusConnection* sys_conn = NULL;
DBusConnection* ses_conn = NULL;
static DBusMessage* the_message = NULL;
osso_context_t *osso;
static LibHalContext *hal_ctx;
static GMainLoop *mainloop;

static ServiceLauncher launcher;

void send_error(const char* s);
static void add_volume(volume_list_t *l, const char *udi);
static void add_prop_watch(const char *udi);
static int get_storage(const char *udi, char **storage_parent,
                       char **storage_udi);
static char *get_dev_name(const char *storage_udi);
static void handle_usb_event(usb_event_t e);
static int mount_volume(volume_list_t *vol);
static volume_list_t *add_usb_volume(volume_list_t *l, const char *udi);
static gboolean usb_unmount_recheck(gpointer data);
static int unmount_usb_volumes(void);
static int launch_fm(void);
static void e_plugged_helper(void);
static void possibly_start_am(void);
static gboolean init_usb_cable_status(gpointer data);
static int mount_usb_volumes(void);
static storage_info_t *storage_from_list(const char *udi);
static mmc_info_t *mmc_from_dev_name(const char *dev);

static void set_usb_mode_key(const char *mode)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_string(gconfclient,
                                     "/system/osso/af/usb-mode",
                                     mode, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_string failed: %s",
                           err->message);
                g_error_free(err);
        }
}

static void update_usb_device_name_key()
{
        const char *s;
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (usb_device_name != NULL) {
                s = usb_device_name;
        } else if (default_usb_device_name != NULL) {
                s = default_usb_device_name;
        } else {
                ULOG_DEBUG_F("no USB name available!");
                return;
        }
        if (!gconf_client_set_string(gconfclient,
                                     "/system/osso/af/usb-device-name",
                                     s, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_string failed: %s",
                           err->message);
                g_error_free(err);
        }
}

static void setup_usb_unmount_pending()
{
        if (usb_pending_timer_id) {
                ULOG_WARN_F("timer was already set");
        } else {
                usb_pending_timer_id =
                        g_timeout_add(MMC_USE_RECHECK_TIMEOUT,
                                      usb_unmount_recheck, NULL);
                /* try ten times */
                usb_unmount_timeout = 10;
        }
}

static void dismantle_usb_unmount_pending()
{
        if (usb_pending_timer_id) {
                if (!g_source_remove(usb_pending_timer_id)) {
                        ULOG_WARN_F("timer did not exist");
                }
                if (usb_dialog != -1) {
                        close_closeable_dialog(usb_dialog);
                }
                usb_pending_timer_id = 0;
                usb_unmount_timeout = 0;
        }
}

static gboolean usb_mount_check(gpointer data)
{
        ULOG_DEBUG_F("entered");
        /* launch FM with what ever we have mounted */
        if (desktop_started && !launch_fm()) {
                /* no USB volumes mounted */
                display_system_note(dgettext(USB_DOMAIN,
                                    "stab_me_usb_no_file_system_available"));
        }
        usb_mounting_timer_id = 0;
        return FALSE;
}

/* this code is necessary for the case where there are no volumes
 * on the memory card */
static gboolean mmc_mount_check(gpointer data)
{
        mmc_info_t *mmc = data;
        volume_list_t *l;
        int found = 0;

        ULOG_DEBUG_F("entered, %s", mmc->name);

        for (l = &mmc->volumes; l != NULL; l = l->next) {
                if (l->udi != NULL) {
                        found = 1;
                        break;
                }
        }
        if (!found) {
                /* no volumes found, treat as unformatted */
                set_mmc_corrupted_flag(TRUE, mmc);
                ULOG_DEBUG_F("set %s", mmc->corrupted_key);
                update_mmc_label(mmc); /* clear old label */
                if (desktop_started) {
                        display_dialog(MSG_MEMORY_CARD_IS_CORRUPTED);
                }
        } else {
                ULOG_DEBUG_F("volumes found, doing nothing");
        }
        mmc->mount_timer_id = 0;
        return FALSE;
}

static gboolean set_desktop_started(gpointer data)
{
        ULOG_DEBUG_F("entered");
        desktop_started = TRUE;
        if (!mmc_initialised) {
                /* initialise GConf keys and possibly mount or USB-share */
                if (int_mmc_enabled) {
                        handle_event(E_INIT_CARD, &int_mmc, NULL);
                }
                handle_event(E_INIT_CARD, &ext_mmc, NULL);
                mmc_initialised = TRUE;
        }
        if (delayed_auto_install_check) {
                possibly_start_am();
                delayed_auto_install_check = FALSE;
        }
        return FALSE;
}

static void dismantle_usb_mount_timeout()
{
        if (usb_mounting_timer_id) {
                if (!g_source_remove(usb_mounting_timer_id)) {
                        ULOG_WARN_F("timer did not exist");
                }
                usb_mounting_timer_id = 0;
                ULOG_DEBUG_F("dismantled USB mounting timeout");
        }
}

static void setup_usb_mount_timeout(int seconds)
{
        if (usb_mounting_timer_id) {
                ULOG_DEBUG_F("resetting the timer");
                g_source_remove(usb_mounting_timer_id);
        } 
        usb_mounting_timer_id = g_timeout_add(seconds * 1000,
                                        usb_mount_check, NULL);
}

static void dismantle_mmc_mount_timeout(mmc_info_t *mmc)
{
        ULOG_DEBUG_F("entered, %s", mmc->name);
        if (mmc->mount_timer_id) {
                g_source_remove(mmc->mount_timer_id);
                mmc->mount_timer_id = 0;
                ULOG_DEBUG_F("dismantled mount timeout for %s", mmc->name);
        }
}

static void setup_mmc_mount_timeout(mmc_info_t *mmc, int seconds)
{
        ULOG_DEBUG_F("entered, %s", mmc->name);
        if (mmc->mount_timer_id) {
                ULOG_DEBUG_F("resetting timer for %s", mmc->name);
                g_source_remove(mmc->mount_timer_id);
        }
        mmc->mount_timer_id = g_timeout_add(seconds * 1000,
                                            mmc_mount_check, mmc);
}

static gboolean usb_unmount_recheck(gpointer data)
{
        assert(usb_pending_timer_id != 0);

        if (!unmount_usb_volumes()) {
                /* re-check later */
                ULOG_DEBUG_F("Some USB volumes are still in use");
                if (--usb_unmount_timeout == 0) {
                        char msg[100];

                        ULOG_DEBUG_F("time out, giving up");
                        dismantle_usb_unmount_pending();

                        snprintf(msg, 100,
                                 dgettext(USB_DOMAIN,
                                   "stab_me_usb_cannot_eject"),
                                 usb_device_name);

                        open_closeable_dialog(OSSO_GN_NOTICE, msg,
                            dgettext(USB_DOMAIN,
                                     "stab_me_usb_cannot_eject_ok"));

                        return FALSE;
                } else {
                        return TRUE;
                }
        } else {
                ULOG_DEBUG_F("All USB volumes unmounted (S_EJECTED)");
                dismantle_usb_unmount_pending();
                set_usb_mode_key("idle"); /* hide the USB plugin */
                usb_state = S_EJECTED;
                return FALSE;
        }
}

static mmc_info_t *mmc_from_dev_name(const char *dev)
{
        if (dev == NULL) return NULL;
        if (ext_mmc.whole_device != NULL
            && strncmp(ext_mmc.whole_device, dev,
                       strlen(ext_mmc.whole_device)) == 0) {
                return &ext_mmc;
        } else if (int_mmc.whole_device != NULL
                   && strncmp(int_mmc.whole_device, dev,
                              strlen(int_mmc.whole_device)) == 0) {
                return &int_mmc;
        } 
        return NULL;
}

static DBusHandlerResult rename_handler(DBusConnection *c,
                                        DBusMessage *m, void *data)
{
        volume_list_t *l;
        mmc_info_t *mmc;
        DBusMessageIter iter;
        char* dev = NULL, *label = NULL;
        const char *udi = NULL;

        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;

        if (!dbus_message_iter_init(m, &iter)) {
                ULOG_ERR_F("no device name argument");
                send_error("no_argument");
                goto rename_exit;
        }
        dbus_message_iter_get_basic(&iter, &dev);
        if (dev == NULL) {
                ULOG_ERR_F("message did not have device name");
                send_error("no_device_name");
                goto rename_exit;
        }
        if (!dbus_message_iter_next(&iter)) {
                ULOG_ERR_F("message did not have second argument");
                send_error("no_second_argument");
                goto rename_exit;
        }

        dbus_message_iter_get_basic(&iter, &label);
        if (label == NULL) {
                ULOG_ERR_F("message did not have label");
                send_error("no_label");
                goto rename_exit;
        }

        mmc = mmc_from_dev_name(dev);
        if (mmc == NULL) {
                ULOG_ERR_F("bad device name '%s'", dev);
                send_error("bad_device_name");
                goto rename_exit;
        }

        send_reply();

        l = &mmc->volumes;
        while (l != NULL) {
                if (l->udi != NULL && l->volume_number == 1) {
                        udi = l->udi;
                        break;
                }
                l = l->next;
        }
        if (udi == NULL) {
                ULOG_ERR_F("could not find udi for first partition");
                goto rename_exit;
        }

        if (label[0] == '\0') {
                /* empty label */
                strncpy(mmc->desired_label, "           ", 11);
        } else {
                strncpy(mmc->desired_label, label, 11);
        }
        mmc->desired_label[11] = '\0';
        ULOG_DEBUG_F("got label: '%s'", mmc->desired_label);
        /* validity of the label is checked later */
        handle_event(E_RENAME, mmc, udi);

rename_exit:
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult format_handler(DBusConnection *c,
                                        DBusMessage *m, void *data)
{
        mmc_info_t *mmc;
        DBusMessageIter iter;
        char* dev = NULL, *label = NULL;

        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;

        if (!dbus_message_iter_init(m, &iter)) {
                ULOG_ERR_F("no device name argument");
                send_error("no_argument");
                goto away;
        }

        dbus_message_iter_get_basic(&iter, &dev);
        if (dev == NULL) {
                ULOG_ERR_F("message did not have device name");
                send_error("no_device_name");
                goto away;
        }
        if (!dbus_message_iter_next(&iter)) {
                ULOG_ERR_F("message did not have second argument");
                send_error("no_second_argument");
                goto away;
        }

        dbus_message_iter_get_basic(&iter, &label);
        if (label == NULL) {
                ULOG_ERR_F("message did not have label");
                send_error("no_label");
                goto away;
        }

        mmc = mmc_from_dev_name(dev);
        if (mmc == NULL) {
                ULOG_ERR_F("bad device name '%s'", dev);
                send_error("bad_device_name");
                goto away;
        }

        send_reply();
        if (label[0] == '\0') {
                /* empty label */
                strncpy(mmc->desired_label, "           ", 11);
        } else {
                strncpy(mmc->desired_label, label, 11);
        }
        mmc->desired_label[11] = '\0';
        ULOG_DEBUG_F("got label: '%s'", mmc->desired_label);
        /* validity of the label is checked later */
        handle_event(E_FORMAT, mmc, NULL);

away:
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult enable_mmc_swap_handler(DBusConnection *c,
                                                 DBusMessage *m,
                                                 void *data)
{
        mmc_info_t *mmc = data;

        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        handle_event(E_ENABLE_SWAP, mmc, NULL);
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult disable_mmc_swap_handler(DBusConnection *c,
                                                  DBusMessage *m,
                                                  void *data)
{
        mmc_info_t *mmc = data;
        DBusMessageIter iter;

        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        if (!dbus_message_iter_init(m, &iter)) {
                ULOG_ERR_F("message does not have argument");
                send_error("required argument missing");
        } else {
                dbus_bool_t b = FALSE;
                if (dbus_message_iter_get_arg_type(&iter) !=
                    DBUS_TYPE_BOOLEAN) {
                        ULOG_ERR_F("argument is not boolean");
                        send_error("argument is not boolean");
                } else {
                        dbus_message_iter_get_basic(&iter, &b);
                        mmc->swap_off_with_close_apps = b;
                        handle_event(E_DISABLE_SWAP, mmc, NULL);
                }
        }
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult card_repair_handler(DBusConnection *c,
                                             DBusMessage *m,
                                             void *data)
{
        DBusMessageIter iter;
        char *s = NULL;
        mmc_info_t *mmc;

        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        if (!dbus_message_iter_init(m, &iter)) {
                ULOG_ERR_F("no device name argument");
                send_error("no_argument");
                goto away;
        }
        dbus_message_iter_get_basic(&iter, &s);

        mmc = mmc_from_dev_name(s);
        if (mmc == NULL) {
                ULOG_ERR_F("bad device name '%s'", s);
                send_error("bad_argument");
        } else {
                send_reply();
                handle_event(E_REPAIR, mmc, NULL);
        }
away:
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

/* This function is for testing only. Allows emulating the USB cable
 * attaching and detaching. */
static DBusHandlerResult test_toggle_usb_cable(DBusConnection *c,
                                               DBusMessage *m,
                                               void *data)
{
        gboolean value = (gboolean)data;

        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        if (value) {
                handle_usb_event(E_ENTER_HOST_MODE);
        } else {
                handle_usb_event(E_CABLE_DETACHED);
        }
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult eject_handler(DBusConnection *c,
                                       DBusMessage *m,
                                       void *data)
{
        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        handle_usb_event(E_EJECT);
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult cancel_eject_handler(DBusConnection *c,
                                              DBusMessage *m,
                                              void *data)
{
        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        handle_usb_event(E_EJECT_CANCELLED);
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

int check_install_file(const mmc_info_t *mmc)
{
        char buf[100];
        int started = 0;

        if (mmc->mount_point == NULL || mmc->mount_point[0] == '\0') {
                return 0;
        }

        snprintf(buf, 100, "%s/%s", mmc->mount_point, ".auto.install");

        if (g_file_test(buf, G_FILE_TEST_EXISTS)) {
                ULOG_DEBUG_F(".auto.install file found");
                if (hildon_mime_open_file(ses_conn, buf) != 1) {
                        ULOG_ERR_F("hildon_mime_open_file failed");
                } else {
                        started = 1;
                }
        }
        return started;
}

static void possibly_start_am(void)
{
        if (!check_install_file(&ext_mmc) && int_mmc_enabled) {
                check_install_file(&int_mmc);
        }
}

/* Handler for checking for the .auto.install file on demand */
static DBusHandlerResult check_auto_install(DBusConnection *c,
                                            DBusMessage *m,
                                            void *data)
{
        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;

        if (!desktop_started || !mmc_initialised) {
                ULOG_DEBUG_F("desktop is not yet running or memory "
                             "cards are not ready");
                delayed_auto_install_check = TRUE;
        } else {
                possibly_start_am();
        }

        send_reply();

        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

#if 0
static void prepare_for_shutdown()
{
        if (usb_state == S_PERIPHERAL) {
                unshare_usb_shared_card(&ext_mmc);
                if (int_mmc_enabled) {
                        unshare_usb_shared_card(&int_mmc);
                }
        }
        /* some cards could be still mounted if they were in use
         * at the time the cable was connected */
        possibly_turn_swap_off_simple(&ext_mmc);
        unmount_volumes(&ext_mmc.volumes);
        if (int_mmc_enabled) {
                possibly_turn_swap_off_simple(&int_mmc);
                unmount_volumes(&int_mmc.volumes);
        }
        unmount_usb_volumes();
}
#endif

static DBusHandlerResult
sig_handler(DBusConnection *c, DBusMessage *m, void *data)
{
    gboolean handled = FALSE;
    /*
    ULOG_DEBUG_L("i|m|p: %s|%s|%s",
               dbus_message_get_interface(m), 
               dbus_message_get_member(m),
               dbus_message_get_path(m));
               */
    the_connection = c;
    the_message = m;
    if (dbus_message_is_signal(m, DBUS_PATH_LOCAL, "Disconnected")) {
        ULOG_INFO_L("D-Bus system bus disconnected, "
                    "unmounting and exiting");
        g_main_loop_quit(mainloop);
        handled = TRUE;
    } else if (dbus_message_is_signal(m, MCE_SIGNAL_IF,
                                      MCE_DEVICELOCK_SIG)) {
	DBusMessageIter iter;
        char* s = NULL;
	dbus_message_iter_init(m, &iter);
	dbus_message_iter_get_basic(&iter, &s);
	if (s == NULL) {
		ULOG_ERR_F("device lock signal did "
                           "not have string argument");
	} else {
	        if (strncmp(s, MCE_LOCKED_STR,
                            strlen(MCE_LOCKED_STR)) == 0) {
                        ULOG_DEBUG_F("device locked signal");
                        device_locked = TRUE;
	        } else {
                        usb_state_t state = get_usb_state();
                        ULOG_DEBUG_F("device unlocked signal");
                        device_locked = FALSE;
                        if (state == S_PERIPHERAL) {
                                /* possibly USB-share cards */
                                handle_usb_event(E_ENTER_PERIPHERAL_MODE);
                        }
	        }
        }
        handled = TRUE;
    } else if (dbus_message_is_signal(m, MCE_SIGNAL_IF,
                                      MCE_SHUTDOWN_SIG)) {
        ULOG_INFO_L("Shutdown signal from MCE, unmounting and exiting");
        g_main_loop_quit(mainloop);
        handled = TRUE;
    } else if (dbus_message_is_signal(m, "org.freedesktop.DBus",
                                      "NameOwnerChanged")) {
        DBusMessageIter iter;
        char *s = NULL;

        if (dbus_message_iter_init(m, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&iter, &s);
            if (s && strcmp(s, DESKTOP_SVC) == 0) {
                ULOG_DEBUG_F("hildon-desktop registered to system bus");
                g_timeout_add(10 * 1000, set_desktop_started, NULL);
            }
        }
        handled = TRUE;
    }
    /* invalidate */
    the_connection = NULL;
    the_message = NULL;
    if (handled) {
        return DBUS_HANDLER_RESULT_HANDLED;
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
}

static void notification_closed(unsigned int id)
{
        if (ext_mmc.dialog_id == id) {
                ext_mmc.dialog_id = -1;
        } else if (ext_mmc.swap_dialog_id == id) {
                ext_mmc.swap_dialog_id = -1;
        } else if (int_mmc_enabled && int_mmc.dialog_id == id) {
                int_mmc.dialog_id = -1;
        } else if (int_mmc_enabled && int_mmc.swap_dialog_id == id) {
                int_mmc.swap_dialog_id = -1;
        } else if (usb_dialog == id) {
                usb_dialog = -1;
        } else {
                ULOG_DEBUG_F("unknown dialog id: %u", id);
        }
}

static void action_invoked(unsigned int id, const char *act)
{
        if (ext_mmc.dialog_id == id) {
                ULOG_DEBUG_F("%s dialog action: '%s'", ext_mmc.name, act);
        } else if (ext_mmc.swap_dialog_id == id) {
                ULOG_DEBUG_F("%s swap dialog action: '%s'",
                             ext_mmc.name, act);
                ext_mmc.swap_dialog_response = strdup(act);
        } else if (int_mmc_enabled && int_mmc.dialog_id == id) {
                ULOG_DEBUG_F("%s dialog action: '%s'", int_mmc.name, act);
        } else if (int_mmc_enabled && int_mmc.swap_dialog_id == id) {
                ULOG_DEBUG_F("%s swap dialog action: '%s'",
                             int_mmc.name, act);
                int_mmc.swap_dialog_response = strdup(act);
        } else if (usb_dialog == id) {
                ULOG_DEBUG_F("USB dialog action: '%s'", act);
                handle_usb_event(E_EJECT_CANCELLED);
        } else {
                ULOG_DEBUG_F("unknown dialog id: %u", id);
        }
}

static DBusHandlerResult
session_sig_handler(DBusConnection *c, DBusMessage *m, void *data)
{
        gboolean handled = FALSE;
        DBusMessageIter iter;
        unsigned int i = 0;
        char *s = NULL;

        /*
        ULOG_DEBUG_F("i|m|p: %s|%s|%s",
                     dbus_message_get_interface(m), 
                     dbus_message_get_member(m),
                     dbus_message_get_path(m));
                     */
        the_connection = c;
        the_message = m;
        if (dbus_message_is_signal(m, FDO_INTERFACE,
                                   "NotificationClosed")) {
                if (dbus_message_iter_init(m, &iter)) {
                        dbus_message_iter_get_basic(&iter, &i);
                        ULOG_DEBUG_F("NotificationClosed for %u", i);
                        notification_closed(i);
                }
                handled = TRUE;
        } else if (dbus_message_is_signal(m, FDO_INTERFACE,
                                          "ActionInvoked")) {
                if (dbus_message_iter_init(m, &iter)) {
                        dbus_message_iter_get_basic(&iter, &i);
                        if (dbus_message_iter_next(&iter)) {
                                dbus_message_iter_get_basic(&iter, &s);
                                ULOG_DEBUG_F("ActionInvoked for %u:"
                                             " '%s'", i, s);
                                action_invoked(i, s);
                        }
                }
                handled = TRUE;
        } else if (dbus_message_is_signal(m, DBUS_PATH_LOCAL,
                                          "Disconnected")) {
                ULOG_INFO_L("D-Bus session bus disconnected, exiting");
                g_main_loop_quit(mainloop);
                handled = TRUE;
        }
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        if (handled) {
                return DBUS_HANDLER_RESULT_HANDLED;
        } else {
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
}

/* This signal makes TN to close applications */
gboolean send_exit_signal(void)
{
        DBusMessage* m = NULL;
        dbus_bool_t ret = FALSE;
        ULOG_DEBUG_F("entering");
        assert(ses_conn != NULL);
        m = dbus_message_new_signal(AK_BROADCAST_OP, AK_BROADCAST_IF,
                                    AK_BROADCAST_EXIT);
        if (m == NULL) {
                ULOG_ERR_F("dbus_message_new_signal failed");
                return FALSE;
        }
        ret = dbus_connection_send(ses_conn, m, NULL);
        if (!ret) {
                ULOG_ERR_F("dbus_connection_send failed");
                dbus_message_unref(m);
                return FALSE;
        }
        dbus_message_unref(m);
        dbus_connection_flush(ses_conn); /* needed because the sleep hack */
        return TRUE;
}

void send_error(const char* n)
{
        DBusMessage* e = NULL;
        assert(the_connection != NULL && the_message != NULL && n != NULL);
        e = dbus_message_new_error(the_message,
                                   "com.nokia.ke_recv.error", n);
        if (e == NULL) {
                ULOG_ERR_F("couldn't create error");
                return;
        }
        if (!dbus_connection_send(the_connection, e, NULL)) {
                ULOG_ERR_F("sending failed");
        }
        dbus_message_unref(e);
}

void send_reply(void)
{
	DBusMessage* e = NULL;
	assert(the_connection != NULL && the_message != NULL);
	e = dbus_message_new_method_return(the_message);
	if (e == NULL) {
		ULOG_ERR_F("couldn't create reply");
		return;
	}
	if (!dbus_connection_send(the_connection, e, NULL)) {
		ULOG_ERR_F("sending failed");
	}
        dbus_message_unref(e);
}

void send_systembus_signal(const char *op, const char *iface,
                           const char *name)
{
        DBusMessage* m;
	assert(sys_conn != NULL);
        m = dbus_message_new_signal(op, iface, name);
        if (m == NULL) {
                ULOG_ERR_F("couldn't create signal %s", name);
                return;
        }
        if (!dbus_connection_send(sys_conn, m, NULL)) {
                ULOG_ERR_F("sending signal %s failed", name);
        }
        dbus_message_unref(m);
}

gboolean get_device_lock(void)
{
        DBusMessageIter iter;
        DBusMessage* m = NULL, *r = NULL;
        DBusError err;
        dbus_bool_t ret = FALSE;
        void* s = NULL;
        assert(sys_conn != NULL);
        dbus_error_init(&err);

        ret = dbus_bus_name_has_owner(sys_conn, MCE_SERVICE, &err);
        if (!ret) {
                if (dbus_error_is_set(&err)) {
                        ULOG_ERR_F("error: %s", err.message);
                        dbus_error_free(&err);
                } else {
                        ULOG_ERR_F("service %s does not exist",
                                   MCE_SERVICE);
                }
                return FALSE;
        }
        m = dbus_message_new_method_call(MCE_SERVICE, MCE_REQUEST_OP,
                MCE_REQUEST_IF, MCE_GET_DEVICELOCK_MSG);
        if (m == NULL) {
                ULOG_ERR_F("couldn't create message");
                return FALSE;
        }
        dbus_error_init(&err);
        r = dbus_connection_send_with_reply_and_block(sys_conn,
                m, -1, &err);
        dbus_message_unref(m);
        if (r == NULL) {
                ULOG_ERR_F("sending failed: %s", err.message);
                dbus_error_free(&err);
                return FALSE;
        }
        dbus_message_iter_init(r, &iter);
        dbus_message_iter_get_basic(&iter, &s);
        if (s == NULL) {
                ULOG_ERR_F("reply did not have string argument");
                dbus_message_unref(r);
                return FALSE;
        }
        if (strncmp((char*)s, MCE_LOCKED_STR, strlen(MCE_LOCKED_STR)) == 0) {
                dbus_message_unref(r);
                return TRUE;
        } else {
                dbus_message_unref(r);
                return FALSE;
        }
}

dbus_uint32_t open_closeable_dialog(osso_system_note_type_t type,
                                    const char *msg, const char *btext)
{
        DBusMessageIter iter;
        DBusMessage* m = NULL, *r = NULL;
        DBusError err;
        dbus_uint32_t id;
        dbus_bool_t ret;

        assert(ses_conn != NULL);

        if (!desktop_started) {
                ULOG_DEBUG_F("do nothing - desktop is not running");
                return -1;
        }
        dbus_error_init(&err);

        m = dbus_message_new_method_call(FDO_SERVICE, FDO_OBJECT_PATH,
                FDO_INTERFACE, "SystemNoteDialog");
        if (m == NULL) {
                ULOG_ERR_F("couldn't create message");
                return -1;
        }
        ret = dbus_message_append_args(m, DBUS_TYPE_STRING, &msg,
                                       DBUS_TYPE_UINT32, &type,
                                       DBUS_TYPE_STRING, &btext,
                                       DBUS_TYPE_INVALID);
        if (!ret) {
                ULOG_ERR_F("couldn't append arguments");
                dbus_message_unref(m);
                return -1;
        }

        r = dbus_connection_send_with_reply_and_block(ses_conn,
                m, -1, &err);
        dbus_message_unref(m);
        if (r == NULL) {
                ULOG_ERR_F("sending failed: %s", err.message);
                dbus_error_free(&err);
                return -1;
        }
        dbus_message_iter_init(r, &iter);
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
                ULOG_ERR_F("reply did not have unsigned int argument");
                dbus_message_unref(r);
                return -1;
        }
        dbus_message_iter_get_basic(&iter, &id);
        dbus_message_unref(r);
        return id;
}

void close_closeable_dialog(dbus_uint32_t id)
{
        DBusMessage* m = NULL, *r = NULL;
        DBusError err;
	dbus_bool_t ret;

        assert(ses_conn != NULL);
        if (id == -1) {
                ULOG_DEBUG_F("do nothing, id == -1");
                return;
        }
        dbus_error_init(&err);

        m = dbus_message_new_method_call(FDO_SERVICE, FDO_OBJECT_PATH,
                FDO_INTERFACE, "CloseNotification");
        if (m == NULL) {
                ULOG_ERR_F("couldn't create message");
                return;
        }
        ret = dbus_message_append_args(m, DBUS_TYPE_UINT32, &id,
			               DBUS_TYPE_INVALID);
	if (!ret) {
		ULOG_ERR_F("couldn't append argument");
                dbus_message_unref(m);
		return;
	}

        r = dbus_connection_send_with_reply_and_block(ses_conn,
                m, -1, &err);
        dbus_message_unref(m);
        if (r == NULL) {
                ULOG_ERR_F("sending failed: %s", err.message);
                dbus_error_free(&err);
                return;
        }
        dbus_message_unref(r);
        return;
}

gint get_dialog_response(dbus_int32_t id)
{
        gint retval = INVALID_DIALOG_RESPONSE;
        DBusMessage* m = NULL, *r = NULL;
        DBusError err;
        DBusMessageIter iter;
	dbus_bool_t ret;

        assert(sys_conn != NULL);
        dbus_error_init(&err);

        m = dbus_message_new_method_call(STATUSBAR_SERVICE, STATUSBAR_OP,
                STATUSBAR_IF, "get_system_dialog_response");
        if (m == NULL) {
                ULOG_ERR_F("couldn't create message");
                return retval;
        }
        ret = dbus_message_append_args(m, DBUS_TYPE_INT32, &id,
			               DBUS_TYPE_INVALID);
	if (!ret) {
		ULOG_ERR_F("couldn't append argument");
                dbus_message_unref(m);
		return retval;
	}

        r = dbus_connection_send_with_reply_and_block(sys_conn,
                m, -1, &err);
        dbus_message_unref(m);
        if (r == NULL) {
                ULOG_ERR_F("sending failed: %s", err.message);
                dbus_error_free(&err);
                return retval;
        }

        if (!dbus_message_iter_init(r, &iter)) {
                ULOG_ERR_F("reply does not have argument");
        } else {
                if (dbus_message_iter_get_arg_type(&iter) !=
                    DBUS_TYPE_INT32) {
                        ULOG_ERR_F("argument is not integer");
                } else {
                        dbus_message_iter_get_basic(&iter, &retval);
                }
        }
        dbus_message_unref(r);
        return retval;
}

static const char* ge2(const char* v, gboolean mandatory)
{
        const char* val = getenv(v);
        if (val == NULL && mandatory) {
                ULOG_CRIT_L("Env. variable '%s' must be defined", v);
                exit(1);
        }
        return val;
}

/* returns first match */
char* find_by_cap_and_prop(const char *capability,
                           const char *property, const char *value)
{
        int num_devices = 0, i;
        char **list, *udi = NULL;
        DBusError error;

        dbus_error_init(&error);
        list = libhal_find_device_by_capability(hal_ctx,
                                                capability,
                                                &num_devices,
                                                &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
                return NULL;
        }
        ULOG_DEBUG_F("number of devices: %d", num_devices);
        for (i = 0; i < num_devices && udi == NULL; ++i) {
                char *prop;

                prop = libhal_device_get_property_string(hal_ctx, 
                         list[i], property, &error);
                if (dbus_error_is_set(&error)) {
                        ULOG_ERR_F("D-Bus error: %s", error.message);
                        dbus_error_free(&error);
                }
                if (prop == NULL) {
                        continue;
                } else if (strcmp(value, prop) != 0) {
                        libhal_free_string(prop);
                        continue;
                }
                libhal_free_string(prop);
                udi = strdup(list[i]);
        }
        libhal_free_string_array(list);
        return udi;
}

char *get_prop_string(const char *udi, const char *property)
{
        DBusError error;
        char *prop;

        assert(udi != NULL);
        assert(property != NULL);

        dbus_error_init(&error);
        prop = libhal_device_get_property_string(hal_ctx, 
                 udi, property, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
        }
        return prop;
}

int get_prop_bool(const char *udi, const char *property)
{
        DBusError error;
        int prop;

        assert(udi != NULL);
        assert(property != NULL);

        dbus_error_init(&error);
        prop = libhal_device_get_property_bool(hal_ctx, 
                 udi, property, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
                return -1;
        }
        return prop ? 1 : 0;
}

int get_prop_int(const char *udi, const char *property)
{
        DBusError error;
        int prop;

        assert(udi != NULL);
        assert(property != NULL);

        dbus_error_init(&error);
        prop = libhal_device_get_property_int(hal_ctx, 
                 udi, property, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
        }
        return prop;
}

/* returns the number of volumes */
int init_mmc_volumes(mmc_info_t *mmc)
{
        int num_devices = 0, i, num_volumes = 0;
        char **list;
        DBusError error;

        if (mmc->storage_udi == NULL) {
                if (!get_storage(mmc->udi, &mmc->storage_parent_udi,
                                 &mmc->storage_udi)) {
                        return 0;
                }
                if (mmc->whole_device != NULL) {
                        free(mmc->whole_device);
                }
                mmc->whole_device = get_dev_name(mmc->storage_udi);
        }

        dbus_error_init(&error);
        list = libhal_manager_find_device_string_match(hal_ctx,
                   "block.storage_device", mmc->storage_udi,
                   &num_devices, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
                if (list != NULL) libhal_free_string_array(list);
                return 0;
        }
        /* the storage is also listed */
        ULOG_DEBUG_F("number of volumes in %s: %d", mmc->name,
                     num_devices - 1);
        for (i = 0; i < num_devices; ++i) {
                if (strcmp(list[i], mmc->storage_udi) == 0) {
                        continue;
                }
                add_volume(&mmc->volumes, list[i]);
                ++num_volumes;
        }
        if (num_volumes == 0) {
                /* no volumes found, treat as unformatted */
                set_mmc_corrupted_flag(TRUE, mmc);
                ULOG_DEBUG_F("set %s", mmc->corrupted_key);
        }
        libhal_free_string_array(list);
        return num_volumes;
}

static void init_usb_volumes()
{
        storage_info_t *si;

        si = storage_list;
        for (; si != NULL; si = si->next) {
                int num_devices = 0, i;
                char **list;
                DBusError error;

                if (si->storage_udi == NULL) {
                        continue;
                }

                dbus_error_init(&error);
                list = libhal_manager_find_device_string_match(hal_ctx,
                           "block.storage_device", si->storage_udi,
                           &num_devices, &error);
                if (dbus_error_is_set(&error)) {
                        ULOG_ERR_F("D-Bus error: %s", error.message);
                        dbus_error_free(&error);
                        if (list != NULL) libhal_free_string_array(list);
                        continue;
                }
                /* the storage is also listed */
                ULOG_DEBUG_F("number of volumes in %s: %d",
                             si->whole_device, num_devices - 1);
                for (i = 0; i < num_devices; ++i) {
                        if (strcmp(list[i], si->storage_udi) == 0) {
                                continue;
                        }
                        add_usb_volume(&si->volumes, list[i]);
                }
                libhal_free_string_array(list);
        }
}

static void init_camera_state()
{
        int state;

        if (camera_out_udi != NULL) {
                state = get_prop_bool(camera_out_udi,
                                      "button.state.value");
                if (state != -1)
                        inform_camera_out(state);
        }
        if (camera_turned_udi != NULL) {
                state = get_prop_bool(camera_turned_udi,
                                      "button.state.value");
                if (state != -1)
                        inform_camera_turned_out(state);
        }
}

static void init_slide_keyboard_state()
{
        int state = TRUE;

        if (slide_keyboard_udi != NULL) {
                state = get_prop_bool(slide_keyboard_udi,
                                      "button.state.value");
        }
        if (state != -1)
                inform_slide_keyboard(state);
}

static int get_storage(const char *udi, char **storage_parent,
                       char **storage_udi)
{
        char *rca_udi;
        char **list;
        int num_devices = 0;
        DBusError error;

        *storage_parent = NULL;
        *storage_udi = NULL;

        dbus_error_init(&error);

        /* find the child */
        list = libhal_manager_find_device_string_match(hal_ctx,
                   "info.parent", udi, &num_devices, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
                return 0;
        }
        if (num_devices > 1) {
                ULOG_WARN_F("%s has more than one children, "
                            "using the first one", udi);
        } else if (num_devices < 1) {
                ULOG_ERR_F("no storage parent found for %s", udi);
                libhal_free_string_array(list);
                return 0;
        }
        rca_udi = strdup(list[0]);
        *storage_parent = rca_udi;
        libhal_free_string_array(list);
                
        /* find the storage if it exists */
        num_devices = 0;
        list = libhal_manager_find_device_string_match(hal_ctx,
                   "info.parent", rca_udi, &num_devices, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
                free(*storage_parent);
                *storage_parent = NULL;
                return 0;
        }
        if (num_devices > 1) {
                ULOG_WARN_F("%s has more than one children, "
                            "using the first one", rca_udi);
        } else if (num_devices < 1) {
                ULOG_ERR_F("no storages found for %s", rca_udi);
                libhal_free_string_array(list);
                free(*storage_parent);
                *storage_parent = NULL;
                return 0;
        }
        *storage_udi = strdup(list[0]);
        libhal_free_string_array(list);
        return 1;
}

static char *get_dev_name(const char *storage_udi)
{
        char *tmp, *dev_name;

        tmp = get_prop_string(storage_udi, "block.device");
        if (tmp == NULL) {
                ULOG_ERR_F("%s has no block.device property", storage_udi);
                return NULL;
        }
        dev_name = strdup(tmp);
        libhal_free_string(tmp);

        return dev_name;
}

static void set_device_name_and_mount_point_key()
{
        GError* err = NULL;
        char *dev;

        if (ext_mmc.mount_point == NULL) {
                ULOG_ERR_F("mount point unknown");
                return;
        }
        assert(gconfclient != NULL);

        gconf_client_unset(gconfclient, "/system/osso/af/mmc-device-name",
                           NULL);
        gconf_client_unset(gconfclient, "/system/osso/af/mmc-mount-point",
                           NULL);

        if (ext_mmc.whole_device == NULL && int_mmc.whole_device) {
                /* ugly: we can guess the name from the internal card's
                 * device name */
                if (strcmp(int_mmc.whole_device, "/dev/mmcblk0") == 0) {
                        dev = "/dev/mmcblk1";
                } else {
                        dev = "/dev/mmcblk0";
                }
        } else if (ext_mmc.whole_device) {
                dev = ext_mmc.whole_device;
        } else {
                ULOG_DEBUG_F("device name unknown");
                return;
        }

        if (!gconf_client_set_string(gconfclient,
            "/system/osso/af/mmc-device-name", dev, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_string failed: %s",
                           err->message);
                g_error_free(err);
                return;
        }
        if (!gconf_client_set_string(gconfclient,
            "/system/osso/af/mmc-mount-point",
            ext_mmc.mount_point, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_string failed: %s",
                           err->message);
                g_error_free(err);
                return;
        }
        mmc_keys_set = TRUE;
}

static int init_card(const char *udi)
{
        mmc_info_t *mmc;
        int internal;
        char *slot;

        slot = get_prop_string(udi, "mmc_host.slot_name");

        if (slot == NULL || strcmp(slot, "slot:external") == 0) {
                mmc = &ext_mmc;
                internal = 0;
        } else if (strcmp(slot, "slot:internal") == 0) {
                mmc = &int_mmc;
                internal = 1;
        } else {
                ULOG_ERR_F("%s has unknown mmc_host.slot_name value: %s",
                           udi, slot);
                libhal_free_string(slot);
                return 0;
        }
        libhal_free_string(slot);

        /* NOTE: keep these in the same order as in the mmc_info_t
         * struct, so that nothing is missed */

        if (internal)
                mmc = &int_mmc;
        else
                mmc = &ext_mmc;

        mmc->internal_card = internal;
        if (internal) {
                strcpy(mmc->name, "int-MMC");
        } else {
                strcpy(mmc->name, "ext-MMC");
        }
        mmc->state = S_INVALID;
        mmc->cover_state = COVER_INVALID;

        mmc->desired_label[0] = '\0';
        if (internal) {
                mmc->mount_point = ge2("INTERNAL_MMC_MOUNTPOINT", TRUE);
                mmc->swap_location = ge2("INTERNAL_MMC_SWAP_LOCATION",
                                         FALSE);
        } else {
                mmc->mount_point = ge2("MMC_MOUNTPOINT", TRUE);
                /* Automatic enabling of swap for external card is disabled
                mmc->swap_location = ge2("MMC_SWAP_LOCATION", FALSE);
                */
                mmc->swap_location = NULL;
        }

        mmc->udi = strdup(udi);
        if (mmc->udi == NULL) {
                ULOG_ERR_F("out of memory");
                return 0;
        }

        if (internal) {
                mmc->volume_label_file = INTERNAL_VOLUME_LABEL_FILE;
                mmc->presence_key = INTERNAL_MMC_PRESENT_KEY;
                mmc->corrupted_key = INTERNAL_MMC_CORRUPTED_KEY;
                mmc->used_over_usb_key = INTERNAL_MMC_USED_OVER_USB_KEY;
                mmc->cover_open_key = INTERNAL_MMC_COVER_OPEN_KEY;
                mmc->swapping_key = "/system/osso/af/internal-mmc-swap";
                mmc->rename_op = INTERNAL_RENAME_OP;
                mmc->format_op = INTERNAL_FORMAT_OP;
                mmc->swap_on_op = INTERNAL_MMC_SWAP_ON_OP;
                mmc->swap_off_op = INTERNAL_MMC_SWAP_OFF_OP;
        } else {
                mmc->volume_label_file = VOLUME_LABEL_FILE;
                mmc->presence_key = MMC_PRESENT_KEY;
                mmc->corrupted_key = MMC_CORRUPTED_KEY;
                mmc->used_over_usb_key = MMC_USED_OVER_USB_KEY;
                mmc->cover_open_key = MMC_COVER_OPEN_KEY;
                mmc->swapping_key = "/system/osso/af/mmc-swap";
                mmc->rename_op = RENAME_OP;
                mmc->format_op = FORMAT_OP;
                mmc->swap_on_op = MMC_SWAP_ON_OP;
                mmc->swap_off_op = MMC_SWAP_OFF_OP;
        }

        if (get_storage(mmc->udi, &mmc->storage_parent_udi,
                        &mmc->storage_udi)) {
                mmc->whole_device = get_dev_name(mmc->storage_udi);
                init_mmc_volumes(mmc);
        } else {
                mmc->whole_device = NULL;
        }

        if (!mmc_keys_set && int_mmc.udi && ext_mmc.udi) {
                /* assumes two memory card slots... */
                set_device_name_and_mount_point_key();
        }

        mmc->cover_udi = strdup(udi);
        ULOG_DEBUG_F("%s cover_udi == %s", mmc->name, mmc->cover_udi);

        if (mmc->cover_udi != NULL) {
                int state;
                state = get_prop_bool(mmc->cover_udi,
                                      "button.state.value");
                if (state) {
                        /* this case also if get_prop_bool() failed */
                        mmc->state = S_COVER_CLOSED;
                } else {
                        mmc->state = S_COVER_OPEN;
                }
        }

        mmc->unmount_pending_timer_id = 0;
        mmc->swap_off_with_close_apps = FALSE;
        mmc->dialog_id = -1;
        mmc->swap_dialog_id = -1;

        return 1;
}

usb_state_t get_usb_state(void)
{
        usb_state_t ret;
        char *prop = NULL;

        if (usb_cable_udi != NULL) {
                prop = get_prop_string(usb_cable_udi, "usb_device.mode");
        }

        if (prop == NULL) {
                ULOG_ERR_F("couldn't read USB mode");
                ret = S_INVALID_USB_STATE;
        } else if (strcmp(prop, "b_peripheral") == 0 ||
                   strcmp(prop, "a_peripheral") == 0) {
                ret = S_PERIPHERAL;
        } else if (strcmp(prop, "a_host") == 0 ||
                   strcmp(prop, "b_host") == 0) {
                ret = S_HOST;
        } else if (strcmp(prop, "b_idle") == 0 ||
                   strcmp(prop, "a_idle") == 0) {
                ret = S_CABLE_DETACHED;
        } else {
                ULOG_ERR_F("unknown USB cable type '%s'", prop);
                ret = S_CABLE_DETACHED;
        }
        if (prop != NULL) libhal_free_string(prop);

        return ret;
}

static void read_config()
{
        int num_hosts = 0, num_devices = 0, i;
        char **list;
        DBusError error;

        dbus_error_init(&error);

        /* Determine the number of memory card slots */
        list = libhal_find_device_by_capability(hal_ctx, "mmc_host",
                                                &num_hosts, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
        }
        ULOG_DEBUG_F("number of mmc_hosts: %d", num_hosts);

        for (i = 0; i < num_hosts; ++i) {
                init_card(list[i]);
        }

        if (int_mmc.udi != NULL) {
                int_mmc_enabled = TRUE;
        }

        /* Global stuff */

        list = libhal_manager_find_device_string_match(hal_ctx,
                 "platform.id", "cam_act", &num_devices, &error);
        if (list != NULL && num_devices == 1) {
                camera_out_udi = strdup(list[0]);
        } else {
                ULOG_ERR_F("coudn't find cam_act");
                camera_out_udi = NULL;
        } 
        libhal_free_string_array(list);

        list = libhal_manager_find_device_string_match(hal_ctx,
                 "platform.id", "cam_turn", &num_devices, &error);
        if (list != NULL && num_devices == 1) {
                camera_turned_udi = strdup(list[0]);
        } else {
                ULOG_ERR_F("coudn't find cam_turn");
                camera_turned_udi = NULL;
        } 
        libhal_free_string_array(list);

        list = libhal_manager_find_device_string_match(hal_ctx,
                 "platform.id", "slide", &num_devices, &error);
        if (list != NULL && num_devices >= 1) {
                slide_keyboard_udi = strdup(list[0]);
        } else {
                ULOG_ERR_F("coudn't find slide");
                slide_keyboard_udi = NULL;
        } 
        libhal_free_string_array(list);

        /* figure out USB cable and mode */
        list = libhal_manager_find_device_string_match(hal_ctx,
                 "button.type", "usb.cable", &num_devices, &error);
        if (list != NULL && num_devices == 1) {
                usb_cable_udi = strdup(list[0]);
        } else {
                ULOG_ERR_F("coudn't find USB cable indicator, using "
                           DEFAULT_USB_CABLE_UDI);
                usb_cable_udi = DEFAULT_USB_CABLE_UDI;
        } 
        libhal_free_string_array(list);

        if (usb_cable_udi != NULL) {
                init_usb_cable_status(NULL);
        } else {
                usb_state = S_CABLE_DETACHED;
                inform_usb_cable_attached(FALSE);
                set_usb_mode_key("idle");
        }
}

static void register_op(DBusConnection *conn, DBusObjectPathVTable *t,
                        const char *op, mmc_info_t *mmc)
{
        dbus_bool_t rc;

        rc = dbus_connection_register_object_path(conn, op, t, mmc);
        if (!rc) {
                ULOG_CRIT_L("Failed to register object path '%s'", op);
                exit(1);
        }
}

static int has_capability(const char *udi, const char *capability)
{
        DBusError error;
        dbus_bool_t ret;

        dbus_error_init(&error);
        ret = libhal_device_query_capability(hal_ctx, udi, capability,
                                             &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("libhal_device_query_capability: %s",
                           error.message);
                dbus_error_free(&error);
                return 0;
        }
        return ret;
}

static usb_state_t check_usb_cable(void)
{
        usb_state_t state;

        state = get_usb_state();
        if (state == S_HOST) {
                handle_usb_event(E_ENTER_HOST_MODE);
        } else if (state == S_PERIPHERAL) {
                handle_usb_event(E_ENTER_PERIPHERAL_MODE);
        } else if (state == S_CABLE_DETACHED) {
                handle_usb_event(E_CABLE_DETACHED);
        }
        return state;
}

static gboolean init_usb_cable_status(gpointer data)
{
        gboolean do_e_plugged = (gboolean)data;

        ULOG_DEBUG_F("entered");
        if (usb_state != S_INVALID_USB_STATE) {
                ULOG_DEBUG_F("usb_state is already valid"); 
                return FALSE;
        }

        usb_state = get_usb_state();
        if (usb_state == S_INVALID_USB_STATE) {
                /* try again later */
                return TRUE;
        } else {
                if (usb_state != S_CABLE_DETACHED) {
                        inform_usb_cable_attached(TRUE);
                } else {
                        inform_usb_cable_attached(FALSE);
                }

                if (usb_state == S_HOST) {
                        mount_usb_volumes();
                        set_usb_mode_key("host");
                } else if (usb_state == S_PERIPHERAL) {
                        if (do_e_plugged) {
                                e_plugged_helper();
                        }
                        set_usb_mode_key("peripheral");
                } else {
                        set_usb_mode_key("idle");
                }
                return FALSE;
        }
}

static void prop_modified(LibHalContext *ctx,
                          const char *udi,
                          const char *key,
                          dbus_bool_t is_removed,
                          dbus_bool_t is_added)
{
        if (is_added) {
                ULOG_DEBUG_F("udi %s added %s", udi, key);
        } else if (is_removed) {
                ULOG_DEBUG_F("udi %s removed %s", udi, key);
        } else {
                int val;

                ULOG_DEBUG_F("udi %s modified %s", udi, key);

                if (strcmp("volume.is_mounted", key) == 0) {
                        return;
                }

                if (strcmp("usb_device.mode", key) == 0) {
                        check_usb_cable();
                        return;
                }

                if (strcmp("button.state.value", key) != 0) {
                        return;
                }

                val = get_prop_bool(udi, "button.state.value");
                if (val == -1) {
                        ULOG_ERR_F("failed to read button.state.value");
                        return;
                }

                if (slide_keyboard_udi != NULL
                    && strcmp(slide_keyboard_udi, udi) == 0) {
                        ULOG_DEBUG_F("SLIDE_KEYBOARD %d", val);
                        inform_slide_keyboard(val);
                } else if (ext_mmc.cover_udi &&
                           strcmp(ext_mmc.cover_udi, udi) == 0) {
                        if (val) {
                                handle_event(E_CLOSED, &ext_mmc, NULL);
                        } else {
                                handle_event(E_OPENED, &ext_mmc, NULL);
                        }
                } else if (int_mmc_enabled
                           && strcmp(int_mmc.cover_udi, udi) == 0) {
                        if (val) {
                                handle_event(E_CLOSED, &int_mmc, NULL);
                        } else {
                                handle_event(E_OPENED, &int_mmc, NULL);
                        }
                } else if (camera_out_udi != NULL
                           && strcmp(camera_out_udi, udi) == 0) {
                        ULOG_DEBUG_F("CAMERA_OUT %d", val);
                        inform_camera_out(val);

                        /* possibly launch something here */
                        if (val &&
                            service_launcher_is_authorized(&launcher)) {
                                service_launcher_launch_services(&launcher);
                        }
                } else if (camera_turned_udi != NULL
                           && strcmp(camera_turned_udi, udi) == 0) {
                        ULOG_DEBUG_F("CAMERA_TURNED %d", val);
                        inform_camera_turned_out(val);
                }
        }
}

static volume_list_t *volume_from_list(volume_list_t *l,
                                       const char *udi)
{
        while (l != NULL) {
                if (l->udi != NULL && strcmp(l->udi, udi) == 0) {
                        return l;
                }
                l = l->next;
        }
        return NULL;
}

static volume_list_t *add_to_volume_list(volume_list_t *l, const char *udi)
{
        char *mp, *dev;

        while (l->udi != NULL) {
                volume_list_t *prev;

                prev = l;
                l = l->next;
                if (l == NULL) {
                        l = calloc(1, sizeof(volume_list_t));
                        if (l == NULL) {
                                ULOG_ERR_F("out of memory");
                                return NULL;
                        }
                        prev->next = l;
                }
        }
        l->udi = strdup(udi);
        if (l->udi == NULL) {
                ULOG_ERR_F("out of memory");
                /* the empty list item is left in place */
                return NULL;
        }
        /* it could have a mount point, in case it's already mounted */
        mp = get_prop_string(l->udi, "volume.mount_point");
        if (mp != NULL) {
                if (*mp != '\0') {
                        /* ugly: strdup() so that free() can be safely used */
                        l->mountpoint = strdup(mp);
                } else {
                        l->mountpoint = NULL;
                }
                libhal_free_string(mp);
        } else {
                l->mountpoint = NULL;
        }

        dev = get_prop_string(l->udi, "block.device");
        if (dev == NULL) {
                ULOG_ERR_F("couldn't get block.device for %s", l->udi);
                free(l->udi);
                l->udi = NULL;
                /* the empty list item is left in place */
                return NULL;
        } else {
                /* ugly: strdup() so that free() can be safely used */
                l->dev_name = strdup(dev);
                libhal_free_string(dev);
        }

        l->corrupt = 0;

        if (get_prop_bool(l->udi, "volume.is_partition") == 1) {
                l->volume_number = get_prop_int(l->udi,
                                                "volume.partition.number");
        } else {
                ULOG_DEBUG_F("volume %s is not a partition", l->udi);
                l->volume_number = 1;
        }
        return l;
}

static void add_volume(volume_list_t *l, const char *udi)
{
        ULOG_DEBUG_F("%s", udi);

        if (volume_from_list(l, udi) != NULL) {
                ULOG_DEBUG_F("%s is already in the list", udi);
                return;
        }

        add_to_volume_list(l, udi);
}

static void zero_volume_info(volume_list_t *l)
{
        if (l->udi != NULL) {
                /* empty the datas */
                free(l->udi);
                l->udi = NULL;
                if (l->mountpoint != NULL) {
                        free(l->mountpoint);
                        l->mountpoint = NULL;
                }
                if (l->dev_name != NULL) {
                        free(l->dev_name);
                        l->dev_name = NULL;
                }
                l->volume_number = 0;
                l->corrupt = 0;
        }
}

static void zero_storage_info(storage_info_t *si)
{
        if (si->storage_udi != NULL) {
                free(si->storage_udi);
                si->storage_udi = NULL;
        }
        if (si->whole_device != NULL) {
                free(si->whole_device);
                si->whole_device = NULL;
        }
        memset(&si->volumes, 0, sizeof(si->volumes));
        si->name[0] = '\0';
}

void clear_volume_list(volume_list_t *l)
{
        for (; l != NULL; l = l->next) {
                zero_volume_info(l);
        }
}

void rm_volume_from_list(volume_list_t *l, const char *udi)
{
        for (; l != NULL; l = l->next) {
                if (l->udi != NULL && strcmp(l->udi, udi) == 0) {
                        zero_volume_info(l);
                        break;
                }
        }
}

static storage_info_t *storage_from_list(const char *udi)
{
        storage_info_t *l;

        l = storage_list;
        while (l != NULL) {
                if (l->storage_udi != NULL
                    && strcmp(l->storage_udi, udi) == 0) {
                        return l;
                }
                l = l->next;
        };
        return NULL;
}

static volume_list_t *usb_volume_from_list(const char *udi)
{
        storage_info_t *si;
        volume_list_t *v = NULL;

        /* try to find the corresponding storage */
        si = storage_list;
        while (si != NULL) {
                if (si->storage_udi != NULL) {
                        v = volume_from_list(&si->volumes, udi);
                        if (v != NULL) {
                                return v;
                        }
                }
                si = si->next;
        }
        ULOG_DEBUG_F("couldn't find storage for %s", udi);
        return NULL;
}

/* Returns 1 on success */
static int usb_mount_helper(volume_list_t *v)
{
        int mounted;
        /* check if someone else has already mounted it */
        mounted = get_prop_bool(v->udi, "volume.is_mounted");
        if (mounted == 1) {
                ULOG_INFO_F("%s is already mounted by someone",
                            v->dev_name);
                /* it's mounted, so not corrupted */
                v->corrupt = 0;
                return 1;
        } else {
                return mount_volume(v);
        }
}

static int usb_storage_count()
{
        int count = 0;
        storage_info_t *si;

        si = storage_list;
        while (si != NULL) {
                if (si->storage_udi != NULL) {
                        ++count;
                }
                si = si->next;
        }
        return count;
}

/* Returns 1 if at least one USB volume is mounted */
static int mount_usb_volumes(void)
{
        int ret = 0, count = 0;
        storage_info_t *si;

        si = storage_list;
        while (si != NULL) {
                if (si->storage_udi != NULL) {
                        volume_list_t *v;

                        v = &si->volumes;
                        while (v != NULL) {
                                if (v->udi != NULL
                                    && v->mountpoint != NULL
                                    && v->dev_name != NULL) {
                                        ++count;
                                        if (usb_mount_helper(v)) {
                                                ret = 1;
                                        }
                                }
                                v = v->next;
                        }
                }
                si = si->next;
        }
        if (count > 0) {
                return ret;
        } else {
                return 1;
        }
}

static int unmount_usb_volumes(void)
{
        int all_unmounted = 1;
        storage_info_t *si;

        si = storage_list;
        while (si != NULL) {
                if (si->storage_udi != NULL) {
                        if (!unmount_volumes(&si->volumes)) {
                                ULOG_WARN("couldn't unmount all volumes"
                                          " for %s", si->storage_udi);
                                all_unmounted = 0;
                        }
                }
                si = si->next;
        }
        return all_unmounted;
}

/* Based on a GnomeVFS patch by Nokia */
static char *make_utf8_clean(const char *name)
{
        char string[200];
        const char *remainder;
        int remaining_bytes;

        string[0] = '\0';
        remainder = name;
        remaining_bytes = strlen(name);
        if (remaining_bytes > 199) {
                remaining_bytes = 199;
        }

        while (remaining_bytes != 0) {
                const char *invalid;
                int valid_bytes;

                if (g_utf8_validate(remainder, remaining_bytes,
                                    &invalid)) {
                        break;
                }
                valid_bytes = invalid - remainder;

                strncat(string, remainder, valid_bytes);
                strcat(string, "?");

                remaining_bytes -= valid_bytes + 1;
                remainder = invalid + 1;
        }

        strcat(string, remainder);

        return strdup(string);
}

static void update_usb_device_name(const char *udi)
{
        /* find first ancestor with info.bus == usb_device */
        char *parent = get_prop_string(udi, "info.parent");

        while (parent != NULL) {
                char *bus, *old_parent;

                bus = get_prop_string(parent, "info.bus");
                if (bus != NULL && strcmp(bus, "usb_device") == 0) {
                        char *name;

                        name = get_prop_string(parent, "info.product");
                        if (name != NULL) {
                                usb_device_name = make_utf8_clean(name);
                                libhal_free_string(name);
                                libhal_free_string(bus);
                                libhal_free_string(parent);
                                break;
                        }
                }
                if (bus != NULL) libhal_free_string(bus);

                old_parent = parent;
                parent = get_prop_string(parent, "info.parent");
                libhal_free_string(old_parent);
        }
        if (usb_device_name == NULL) {
                usb_device_name = strdup(default_usb_device_name);
        }
        ULOG_DEBUG_F("USB device name is '%s'", usb_device_name);
        update_usb_device_name_key();
}

static void update_storage_info(const char *udi, storage_info_t *si)
{
        char *dev;

        dev = get_dev_name(udi);
        if (dev == NULL) {
                ULOG_WARN_F("couldn't get block.device for %s", udi);
                /* TODO: invalidate it? */
                return;
        }
        if (si->whole_device != NULL) {
                free(si->whole_device);
        }
        si->whole_device = dev;
        snprintf(si->name, 10, "%s", dev);

        if (si->storage_udi == NULL) {
                si->storage_udi = strdup(udi);
        }

        /* update USB device name if needed */
        if (usb_device_name == NULL) {
                update_usb_device_name(udi);
        }
}

static void add_usb_storage(const char *udi)
{
        storage_info_t *si, *l;

        ULOG_DEBUG_F("entered, udi=%s", udi);

        if ((si = storage_from_list(udi)) != NULL) {
                ULOG_DEBUG_F("%s is already in the list, "
                             "updating the information", udi);
                update_storage_info(udi, si); 
                return;
        }

        si = calloc(1, sizeof(storage_info_t));
        if (si == NULL) {
                ULOG_ERR_F("out of memory");
                return;
        }

        l = storage_list;
        if (l != NULL) {
                while (l->next != NULL) {
                        l = l->next;
                }
                l->next = si;
        } else {
                storage_list = si;
        }

        update_storage_info(udi, si);
}

/* Returns 1 on success */
static int mount_volume(volume_list_t *vol)
{
        const char* mount_args[] = {MMC_MOUNT_COMMAND, NULL, NULL,
                                     NULL};
        int ret = -1;

        mount_args[1] = vol->dev_name;
        mount_args[2] = vol->mountpoint;

        ret = exec_prog(MMC_MOUNT_COMMAND, mount_args);
        if (ret != 0) {
                ULOG_DEBUG_F("exec_prog returned %d", ret);
                /* TODO: handle corruptness */
                return 0;
        } else {
                vol->corrupt = 0;
                return 1;
        }
}

static int unmount_volume(volume_list_t *vol)
{
        const char* umount_args[] = {MMC_UMOUNT_COMMAND, NULL, NULL};
        int ret;

        if (vol->mountpoint == NULL) {
                ULOG_WARN_F("mount point not known");
                return 1;
        }

        umount_args[1] = vol->mountpoint;
        ret = exec_prog(MMC_UMOUNT_COMMAND, umount_args);
        if (ret == 0) {
                return 1;
        } else {
                return 0;
        }
}

static storage_info_t *storage_for_volume(const char *udi)
{
        storage_info_t *si;
        char *sudi;

        sudi = get_prop_string(udi, "block.storage_device");
        if (sudi == NULL) {
                ULOG_WARN_F("couldn't get block.storage_device for %s", udi);
                return NULL;
        }

        si = storage_from_list(sudi);
        if (si == NULL) {
                ULOG_DEBUG_F("storage for %s is not available", udi);
                /* might be a volume that is not a USB mass storage */
                libhal_free_string(sudi);
                return NULL;
        }
        libhal_free_string(sudi);
        return si;
}

static volume_list_t *add_usb_volume(volume_list_t *l, const char *udi)
{
        volume_list_t *vol;

        ULOG_DEBUG_F("entered, udi=%s", udi);

        vol = add_to_volume_list(l, udi);
        if (vol == NULL) {
                return NULL;
        }

        /* construct a mount point if needed */
        if (vol->mountpoint == NULL && vol->dev_name != NULL) {
                char *dev_copy = strdup(vol->dev_name);
                if (dev_copy != NULL) {
                        char buf[100];
                        snprintf(buf, 100, "/media/usb/%s",
                                 basename(dev_copy));
                        free(dev_copy);
                        vol->mountpoint = strdup(buf);
                } else {
                        ULOG_WARN_F("strdup failed");
                }
        }
        return vol;
}

static int parent_and_child(const char *parent, const char *child)
{
        char *prop;

        if (parent == NULL || child == NULL) {
                return 0;
        }

        prop = get_prop_string(child, "info.parent");
        if (prop == NULL) {
                return 0;
        }
        if (strcmp(prop, parent) == 0) {
                libhal_free_string(prop);
                return 1;
        }
        libhal_free_string(prop);
        return 0;
}

static int volume_slot_number(const char *udi)
{
        if (ext_mmc.storage_udi != NULL) {
                if (parent_and_child(ext_mmc.storage_udi, udi)) {
                        return 1;
                }
        }

        if (int_mmc_enabled && int_mmc.storage_udi != NULL) {
                if (parent_and_child(int_mmc.storage_udi, udi)) {
                        return 0;
                }
        }

        return -1;
}

static void add_storage_for_mmc(mmc_info_t *mmc,
                                const char *storage_parent_udi,
                                const char *storage_udi)
{
        char *dev;

        ULOG_DEBUG_F("%s is storage for %s", storage_udi, mmc->name);
        if (mmc->storage_parent_udi == NULL) {
                mmc->storage_parent_udi = strdup(storage_parent_udi);
                if (mmc->storage_parent_udi == NULL) {
                        ULOG_ERR_F("out of memory");
                        return;
                }
        }
        if (mmc->storage_udi != NULL) {
                ULOG_WARN_F("storage_udi was non-NULL");
                free(mmc->storage_udi);
        }
        mmc->storage_udi = strdup(storage_udi);
        if (mmc->storage_udi == NULL) {
                ULOG_ERR_F("out of memory");
                return;
        }

        dev = get_dev_name(storage_udi);
        if (dev == NULL) {
                ULOG_ERR_F("couldn't get block.device for %s",
                           storage_udi);
                return;
        }
        if (mmc->whole_device != NULL) {
                ULOG_WARN_F("whole_device was non-NULL");
                free(mmc->whole_device);
        }
        mmc->whole_device = dev;

        if (!mmc_keys_set) {
                set_device_name_and_mount_point_key();
        }

        handle_event(E_DEVICE_ADDED, mmc, storage_udi);
}

static void init_usb_storages()
{
        int num_devices = 0;
        char **list;
        DBusError error;
        int i, count = 0;

        dbus_error_init(&error);

        list = libhal_find_device_by_capability(hal_ctx, "storage",
                                                &num_devices, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_ERR_F("D-Bus error: %s", error.message);
                dbus_error_free(&error);
        }

        for (i = 0; i < num_devices; ++i) {
                char *udi = list[i];
                char *s = get_prop_string(udi, "storage.bus");
                if (s != NULL && strcmp(s, "usb") == 0) {
                        add_usb_storage(udi);
                        ++count;
                }
                if (s != NULL) libhal_free_string(s);
        }
        ULOG_DEBUG_F("%d storages of which %d are USB storages",
                     num_devices, count);
        if (count == 0) {
                /* set default value */
                update_usb_device_name_key();
        }
        libhal_free_string_array(list);
}

static void device_added(LibHalContext *ctx, const char *udi)
{
        ULOG_DEBUG_F("udi: %s", udi);

        if (has_capability(udi, "volume")) {
                int slot = volume_slot_number(udi);
                storage_info_t *si;

                if (slot == 1) {
                        ULOG_DEBUG_F("%s is volume for %s", udi,
                                     ext_mmc.name);
                        add_volume(&ext_mmc.volumes, udi);
                        dismantle_mmc_mount_timeout(&ext_mmc);
                        handle_event(E_VOLUME_ADDED, &ext_mmc, udi);
                } else if (int_mmc_enabled && slot == 0) {
                        ULOG_DEBUG_F("%s is volume for %s", udi,
                                     int_mmc.name);
                        add_volume(&int_mmc.volumes, udi);
                        dismantle_mmc_mount_timeout(&int_mmc);
                        handle_event(E_VOLUME_ADDED, &int_mmc, udi);
                } else if ((si = storage_for_volume(udi)) != NULL) {
                        volume_list_t *vol;
                        vol = add_usb_volume(&si->volumes, udi);
                        if (vol != NULL && vol->dev_name != NULL
                            && vol->mountpoint != NULL
                            && usb_state == S_HOST) {
                                ULOG_DEBUG_F("mounting USB VOLUME %s to %s",
                                             vol->dev_name, vol->mountpoint);
                                mount_volume(vol);

                                /* refresh the mounting timer to give
                                 * more time to possible subsequent
                                 * volumes */
                                setup_usb_mount_timeout(1);
                        }
                }
        } else if (has_capability(udi, "storage")) {
                char *parent, *grandparent = NULL;

                parent = get_prop_string(udi, "info.parent");
                if (parent != NULL) {
                        grandparent = get_prop_string(parent, "info.parent");
                }
                if (grandparent == NULL) {
                        ULOG_DEBUG_F("storage didn't have grandparent");
                        if (parent != NULL) libhal_free_string(parent);
                        return;
                }

                if (strcmp(ext_mmc.udi, grandparent) == 0) {
                        add_storage_for_mmc(&ext_mmc, parent, udi);
                        setup_mmc_mount_timeout(&ext_mmc, 5);
                } else if (int_mmc_enabled &&
                           strcmp(int_mmc.udi, grandparent) == 0) {
                        add_storage_for_mmc(&int_mmc, parent, udi);
                        setup_mmc_mount_timeout(&int_mmc, 5);
                } else {
                        char *s = get_prop_string(udi, "storage.bus");
                        if (s != NULL && strcmp(s, "usb") == 0) {
                                add_usb_storage(udi);

                                if (usb_state == S_HOST) {
                                        /* refresh the mounting timer to
                                         * give more time to possible
                                         * subsequent volumes */
                                        setup_usb_mount_timeout(5);
                                }
                        }
                        if (s != NULL) libhal_free_string(s);
                }
                libhal_free_string(parent);
                libhal_free_string(grandparent);
        }
}

static void device_removed(LibHalContext *ctx, const char *udi)
{
        volume_list_t *vol;
        storage_info_t *si;

        ULOG_DEBUG_F("udi: %s", udi);

        /* check if it's one of the memory card storages */
        if (ext_mmc.storage_udi != NULL
            && strcmp(ext_mmc.storage_udi, udi) == 0) {
                ULOG_DEBUG_F("storage for %s removed", ext_mmc.name);
                dismantle_mmc_mount_timeout(&ext_mmc);
                handle_event(E_DEVICE_REMOVED, &ext_mmc, udi);
                free(ext_mmc.storage_udi);
                ext_mmc.storage_udi = NULL;
                free(ext_mmc.whole_device);
                ext_mmc.whole_device = NULL;
                return;
        } else if (int_mmc_enabled && int_mmc.storage_udi != NULL
                   && strcmp(int_mmc.storage_udi, udi) == 0) {
                ULOG_DEBUG_F("storage for %s removed", int_mmc.name);
                dismantle_mmc_mount_timeout(&int_mmc);
                handle_event(E_DEVICE_REMOVED, &int_mmc, udi);
                free(int_mmc.storage_udi);
                int_mmc.storage_udi = NULL;
                free(int_mmc.whole_device);
                int_mmc.whole_device = NULL;
                return;
        }

        /* check if it's one of the memory card storage parents */
        if (ext_mmc.storage_parent_udi != NULL
            && strcmp(ext_mmc.storage_parent_udi, udi) == 0) {
                ULOG_DEBUG_F("storage parent for %s removed",
                             ext_mmc.name);
                free(ext_mmc.storage_parent_udi);
                ext_mmc.storage_parent_udi = NULL;
                return;
        } else if (int_mmc_enabled && int_mmc.storage_parent_udi != NULL
                   && strcmp(int_mmc.storage_parent_udi, udi) == 0) {
                ULOG_DEBUG_F("storage parent for %s removed",
                             int_mmc.name);
                free(int_mmc.storage_parent_udi);
                int_mmc.storage_parent_udi = NULL;
                return;
        }

        /* check if it's one of our volumes */
        if (volume_from_list(&ext_mmc.volumes, udi) != NULL) {
                handle_event(E_VOLUME_REMOVED, &ext_mmc, udi);
                return;
        } else if (int_mmc_enabled
                   && volume_from_list(&int_mmc.volumes, udi) != NULL) {
                handle_event(E_VOLUME_REMOVED, &int_mmc, udi);
                return;
        }

        /* check if it's one of the USB mass storages */
        si = storage_from_list(udi);
        if (si != NULL) {
                ULOG_DEBUG_F("USB STORAGE REMOVED %s", udi);
                zero_storage_info(si);
                if (usb_storage_count() == 0
                    && usb_device_name != NULL) {
                        /* invalidate the device name */
                        free(usb_device_name);
                        usb_device_name = NULL;
                }
        } else if ((vol = usb_volume_from_list(udi)) != NULL) {
                ULOG_DEBUG_F("USB VOLUME REMOVED %s", udi);
                if (usb_state == S_HOST) {
                        if (!unmount_volume(vol)) {
                                ULOG_WARN_F("couldn't unmount USB "
                                            "VOLUME %s", udi);
                        }
                }
                zero_volume_info(vol);
        }
}

static usb_state_t try_eject()
{
        char msg[100];

        /* ensure that there is at least the default name */
        if (usb_device_name == NULL) {
                usb_device_name = strdup(default_usb_device_name);
        }
        snprintf(msg, 100,
                 dgettext(USB_DOMAIN, "stab_me_usb_ejecting"),
                 usb_device_name);

        /* Cancel note with progress indicator is type 4 */
        usb_dialog = open_closeable_dialog(4, msg, "");

        if (unmount_usb_volumes()) {
                /* success, all unmounted */

                close_closeable_dialog(usb_dialog);
                usb_dialog = -1;

                snprintf(msg, 100,
                         dgettext(USB_DOMAIN, "stab_me_usb_ejected"),
                         usb_device_name);
                display_dialog(msg);
                set_usb_mode_key("idle"); /* hide the USB plugin */
                return S_EJECTED;
        } else {
                setup_usb_unmount_pending();
                return S_EJECTING;
        }
}

static int open_fm_folder(const char *folder)
{
        DBusMessage* m = NULL;
        dbus_bool_t r;

        assert(ses_conn != NULL);

        m = dbus_message_new_method_call("com.nokia.osso_filemanager",
                "/com/nokia/osso_filemanager",
                "/com/nokia/osso_filemanager", "open_folder");
        if (m == NULL) {
                ULOG_ERR_F("couldn't create message");
                return 0;
        }

        if (!dbus_message_append_args(m, DBUS_TYPE_STRING, &folder,
                                      DBUS_TYPE_INVALID)) {
                ULOG_ERR_F("dbus_message_append_args failed");
                dbus_message_unref(m);
                return 0;
        }

        r = dbus_connection_send(ses_conn, m, NULL);
        dbus_message_unref(m);
        if (!r) {
                ULOG_ERR_F("dbus_connection_send failed");
                return 0;
        }
        return 1;
}

static int launch_fm(void)
{
        storage_info_t *si;
        char *mount_point = NULL;
        char buf[100];
        int ret;

        /* try to find a volume that is mounted and has smallest
         * volume number. FIXME: this algorithm is really stupid... */
        si = storage_list;
        for (; si != NULL; si = si->next) {
                int n;

                if (si->storage_udi == NULL) {
                        continue;
                }

                /* only eight first volumes are considered */
                for (n = 1; n <= 8; ++n) {
                        volume_list_t *l = &si->volumes;
                        while (l != NULL) {
                                if (l->udi != NULL
                                    && l->volume_number == n) {
                                        int mounted;
                                        mounted = get_prop_bool(l->udi,
                                                      "volume.is_mounted");
                                        if (mounted == 1) {
                                                mount_point = l->mountpoint;
                                                goto exit_loops;
                                        }
                                        break;
                                }
                                l = l->next;
                        }
                }
        }
exit_loops:

        if (mount_point == NULL) {
                ULOG_DEBUG_F("couldn't find suitable mount point");
                return 0;
        }

        snprintf(buf, 100, "file://%s", mount_point);

        ULOG_DEBUG_F("opening '%s' in File Manager", buf);
        ret = open_fm_folder(buf);
        if (!ret) {
                ULOG_ERR_F("open_fm_folder failed");
                return 1;
        }
        return 1;
}

static void e_plugged_helper(void)
{
        int er = 1, ir = 1;

        if (ext_mmc.whole_device == NULL
            && int_mmc.whole_device == NULL) {
                ULOG_DEBUG_F("no cards inserted");
                display_system_note(MSG_NO_MEMORY_CARD_INSERTED);
                return;
        }

        /* false means failure */
        if (int_mmc_enabled) {
                /* handle internal card first so that it gets the
                 * first available drive letter in Windows */
                ir = handle_event(E_PLUGGED, &int_mmc, NULL);
        }
        er = handle_event(E_PLUGGED, &ext_mmc, NULL);

        if (!er && !ir) {
                show_usb_sharing_failed_dialog(&int_mmc, &ext_mmc);
        } else if (!er) {
                show_usb_sharing_failed_dialog(NULL, &ext_mmc);
        } else if (!ir) {
                show_usb_sharing_failed_dialog(&int_mmc, NULL);
        }
}

static void handle_usb_event(usb_event_t e)
{
        switch (e) {
                case E_CABLE_DETACHED:
                        set_usb_mode_key("idle");
                        inform_usb_cable_attached(FALSE);
                        if (usb_state == S_HOST) {
                                ULOG_DEBUG_F("E_CABLE_DETACHED in S_HOST");
                                dismantle_usb_mount_timeout();
                                unmount_usb_volumes();
                        } else if (usb_state == S_PERIPHERAL) {
                                ULOG_DEBUG_F("E_CABLE_DETACHED in"
                                             " S_PERIPHERAL");
                                handle_event(E_DETACHED, &ext_mmc, NULL);
                                if (int_mmc_enabled) {
                                        handle_event(E_DETACHED, &int_mmc,
                                                     NULL);
                                }
                                if (ext_mmc.whole_device != NULL
                                    || int_mmc.whole_device != NULL) {
                                        display_dialog(
                                                MSG_USB_DISCONNECTED);
                                }
                        } else if (usb_state == S_EJECTED) {
                                ULOG_DEBUG_F("E_CABLE_DETACHED in S_EJECTED");
                        } else if (usb_state == S_EJECTING) {
                                ULOG_INFO_F("E_CABLE_DETACHED in S_EJECTING");
                                dismantle_usb_unmount_pending();
                                unmount_usb_volumes();
                        } else {
                                ULOG_WARN_F("E_CABLE_DETACHED in %d",
                                            usb_state);
                        }
                        usb_state = S_CABLE_DETACHED;
                        break;
                case E_EJECT:
                        if (usb_state == S_HOST) {
                                ULOG_DEBUG_F("E_EJECT in S_HOST");
                                dismantle_usb_mount_timeout();
                                send_reply();
                                usb_state = try_eject();
                        } else if (usb_state == S_EJECTING) {
                                ULOG_DEBUG_F("E_EJECT in S_EJECTING");
                                send_reply();
                                if (usb_pending_timer_id == 0) {
                                        usb_state = try_eject();
                                } else {
                                        ULOG_DEBUG_F(
                                            "polling already set up");
                                }
                        } else {
                                ULOG_WARN_F("E_EJECT in %d", usb_state);
                                send_error("improper state");
                        }
                        break;
                case E_EJECT_CANCELLED:
                        if (usb_state == S_EJECTING) {
                                ULOG_DEBUG_F("E_EJECT_CANCELLED in"
                                             " S_EJECTING");
                                dismantle_usb_unmount_pending();
                                send_reply();

                                /* possibly re-mount already unmounted
                                 * volumes */
                                mount_usb_volumes();

                                usb_state = S_HOST;
                        } else {
                                ULOG_WARN_F("E_EJECT_CANCELLED in %d",
                                            usb_state);
                                send_error("improper state");
                        }
                        break;
                case E_ENTER_HOST_MODE:
                        set_usb_mode_key("host");
                        inform_usb_cable_attached(TRUE);
                        if (usb_state == S_CABLE_DETACHED) {
                                ULOG_DEBUG_F("E_ENTER_HOST_MODE in "
                                             "S_CABLE_DETACHED");
                                /* mounting happens later when the volumes
                                 * are detected */
                                setup_usb_mount_timeout(15);
                                usb_state = S_HOST;
                        } else {
                                ULOG_WARN_F("E_ENTER_HOST_MODE in %d",
                                            usb_state);
                        }
                        break;
                case E_ENTER_PERIPHERAL_MODE:
                        /* clear the name */
                        free(usb_device_name);
                        usb_device_name = NULL;
                        update_usb_device_name_key();

                        set_usb_mode_key("peripheral");
                        inform_usb_cable_attached(TRUE);
                        if (usb_state == S_CABLE_DETACHED
                            || usb_state == S_PERIPHERAL) {
                                /* we could be in S_PERIPHERAL already
                                 * because of the device lock */
                                ULOG_DEBUG_F("E_ENTER_PERIPHERAL_MODE"
                                             " in S_CABLE_DETACHED or "
                                             "S_PERIPHERAL");
                                usb_state = S_PERIPHERAL;
                                e_plugged_helper();
                        } else {
                                ULOG_WARN_F("E_ENTER_PERIPHERAL_MODE in %d",
                                            usb_state);
                        }
                        break;
                default:
                        ULOG_ERR_F("unknown event %d", e);
        }
}

static void add_prop_watch(const char *udi)
{
        DBusError error;
        if (udi == NULL) {
                return;
        }
        dbus_error_init(&error);
        if (!libhal_device_add_property_watch(hal_ctx, udi, &error)) {
                ULOG_ERR_F("libhal_device_add_property_watch failed: %s",
                           error.message);
                dbus_error_free(&error);
        }
}

int get_cable_peripheral(void)
{
        return usb_state == S_PERIPHERAL;
}

static void sigterm(int signo)
{
        ULOG_INFO_L("got SIGTERM");
        g_main_loop_quit(mainloop);
}

/* Does initialisations and goes to the Glib main loop. */
int main(int argc, char* argv[])
{
        DBusError error;
        DBusConnection *conn = NULL;
        DBusObjectPathVTable vtable = {
	        .message_function = NULL,
	        .unregister_function = NULL
        };
        int ret, first_boot = FALSE;

        if (signal(SIGTERM, sigterm) == SIG_ERR) {
                ULOG_CRIT_L("signal() failed");
        }

        g_type_init();
        mainloop = g_main_loop_new(NULL, TRUE);
        ULOG_OPEN(APPL_NAME);

        dbus_error_init(&error);

        if (setlocale(LC_ALL, "") == NULL) {
	        ULOG_ERR_L("couldn't set locale");
        }
        if (bindtextdomain(PACKAGE, LOCALEDIR) == NULL) {
                ULOG_ERR_L("bindtextdomain() failed");
        }
        if (textdomain(PACKAGE) == NULL) {
      	        ULOG_ERR_L("textdomain() failed");
        }

        osso = osso_initialize(APPL_NAME, APPL_VERSION, FALSE,
                               g_main_context_default());
        if (osso == NULL) {
                ULOG_CRIT_L("Libosso initialisation failed");
                exit(1);
        }
        ses_conn = (DBusConnection*) osso_get_dbus_connection(osso);
        if (ses_conn == NULL) {
                ULOG_CRIT_L("osso_get_dbus_connection() failed");
                exit(1);
        }

        /* set up filter for org.freedesktop.Notifications signals */
        if (!dbus_connection_add_filter(ses_conn, session_sig_handler,
                                        NULL, NULL)) {
                ULOG_CRIT_L("Failed to register signal handler callback");
	        exit(1);
        }

        dbus_bus_add_match(ses_conn, "type='signal',interface='"
                           FDO_INTERFACE "'", &error);
        if (dbus_error_is_set(&error)) {
                ULOG_CRIT_L("dbus_bus_add_match failed");
	        exit(1);
        }

        service_launcher_init(&launcher);

        conn = (DBusConnection*) osso_get_sys_dbus_connection(osso);
        if (conn == NULL) {
                ULOG_CRIT_L("Failed to get system bus connection");
                exit(1);
        }
        sys_conn = conn;

        /* initialise HAL context */
        if (!(hal_ctx = libhal_ctx_new())) {
                ULOG_CRIT_L("libhal_ctx_new() failed");
                exit(1);
        }
        if (!libhal_ctx_set_dbus_connection(hal_ctx, sys_conn)) {
                ULOG_CRIT_L("libhal_ctx_set_dbus_connection() failed");
                exit(1);
        }
        if (!libhal_ctx_init(hal_ctx, &error)) {
                if (dbus_error_is_set(&error)) {
                        ULOG_CRIT_L("libhal_ctx_init: %s: %s", error.name,
                                    error.message);
                        dbus_error_free(&error);
                }
                ULOG_CRIT_L("Could not initialise connection to hald");
                exit(1);
        }

        do_global_init();
        read_config();

        /* Set the callback for when a property is modified on a device */
        ret = libhal_ctx_set_device_property_modified(hal_ctx,
                                                      prop_modified);
        if (!ret) {
                ULOG_CRIT_L("libhal_ctx_set_device_property_modified failed");
                exit(1);
        }

        if (!dbus_connection_add_filter(conn, sig_handler, NULL, NULL)) {
                ULOG_CRIT_L("Failed to register signal handler callback");
	        exit(1);
        }

        dbus_bus_add_match(conn, MCE_MATCH_RULE, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_CRIT_L("dbus_bus_add_match for %s failed",
                            MCE_MATCH_RULE);
	        exit(1);
        }
        dbus_bus_add_match(conn, "type='signal',member='NameOwnerChanged'",
                           &error);
        if (dbus_error_is_set(&error)) {
                ULOG_CRIT_L("dbus_bus_add_match failed");
	        exit(1);
        }

        /* register D-BUS interface for MMC renaming */
        vtable.message_function = rename_handler;
        register_op(ses_conn, &vtable, RENAME_OP, NULL);

        /* register D-BUS interface for MMC formatting */
        vtable.message_function = format_handler;
        register_op(ses_conn, &vtable, FORMAT_OP, NULL);

        /* register D-BUS interface for enabling swapping on MMC */
        vtable.message_function = enable_mmc_swap_handler;
        /* FIXME
        register_op(sys_conn, &vtable, ext_mmc.swap_on_op, &ext_mmc);
        */
        if (int_mmc_enabled) {
                register_op(sys_conn, &vtable, int_mmc.swap_on_op, &int_mmc);
        }

        /* register D-BUS interface for disabling swapping on MMC */
        vtable.message_function = disable_mmc_swap_handler;
        /* FIXME
        register_op(sys_conn, &vtable, ext_mmc.swap_off_op, &ext_mmc);
        */
        if (int_mmc_enabled) {
                register_op(sys_conn, &vtable, int_mmc.swap_off_op, &int_mmc);
        }

        /* register D-Bus interface for emulating the USB cable */
        vtable.message_function = test_toggle_usb_cable;
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/test_usb_cable_on", (void*)1);
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/test_usb_cable_off", (void*)0);

        /* D-Bus interface for checking the .auto.install file */
        vtable.message_function = check_auto_install;
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/check_auto_install", NULL);

        /* D-Bus interface for repairing memory cards */
        vtable.message_function = card_repair_handler;
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/repair_card", NULL);

        /* D-Bus interface for 'ejecting' USB mass storages */
        vtable.message_function = eject_handler;
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/usb_eject", NULL);

        /* D-Bus interface for cancelling 'ejecting' of USB mass storages */
        vtable.message_function = cancel_eject_handler;
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/usb_cancel_eject", NULL);

        add_prop_watch(ext_mmc.cover_udi);
        add_prop_watch(int_mmc.cover_udi);
        add_prop_watch(camera_out_udi);
        add_prop_watch(camera_turned_udi);
        add_prop_watch(slide_keyboard_udi);
        add_prop_watch(usb_cable_udi);

        if (!libhal_ctx_set_device_added(hal_ctx, device_added)) {
                ULOG_CRIT_L("libhal_ctx_set_device_added failed");
                exit(1);
        }
        if (!libhal_ctx_set_device_removed(hal_ctx, device_removed)) {
                ULOG_CRIT_L("libhal_ctx_set_device_removed failed");
                exit(1);
        }

        init_camera_state();
        init_slide_keyboard_state();

        if (default_usb_device_name == NULL) {
                default_usb_device_name =
                        strdup(dgettext(USB_DOMAIN,
                                        "stab_me_usb_device_name"));
        }
        init_usb_storages();
        init_usb_volumes();

        /* check if hildon-desktop is running */
        if (dbus_bus_name_has_owner(sys_conn, DESKTOP_SVC, NULL)) {
                ULOG_DEBUG_F("hildon-desktop is running");
                desktop_started = TRUE;
        }
        if (getenv("FIRST_BOOT") != NULL) {
                ULOG_DEBUG_F("this is the first boot");
                first_boot = TRUE;
        }

        if (desktop_started || first_boot) {
                /* initialise GConf keys and possibly mount or USB-share */
                if (int_mmc_enabled) {
                        handle_event(E_INIT_CARD, &int_mmc, NULL);
                }
                handle_event(E_INIT_CARD, &ext_mmc, NULL);
                mmc_initialised = TRUE;
        }

        if (usb_state == S_INVALID_USB_STATE) {
                /* check it again later */
                g_timeout_add(1000, init_usb_cable_status, (void*)1);
        }

        g_main_loop_run(mainloop); 
        ULOG_DEBUG_L("Returned from the main loop");
        /*
        prepare_for_shutdown();
        */
        service_launcher_deinit(&launcher);
    
        exit(0);
}

