/**
  @file ke-recv.c

  This file is part of ke-recv.

  Copyright (C) 2004-2009 Nokia Corporation. All rights reserved.
  Copyright (C) 2012 Pali Rohár <pali.rohar@gmail.com>

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

#include "ke-recv.h"
#include "exec-func.h"
#include "gui.h"
#include "events.h"
#include "camera.h"
#include "udev-helper.h"
#include <hildon-mime.h>
#include <libgen.h>


#define FDO_INTERFACE "org.freedesktop.Notifications"
#define DESKTOP_IF "com.nokia.HildonDesktop"


// Set in events.c
extern GConfClient* gconfclient;


/* "the connection" is the connection where "the message" (i.e.
   the current message or signal that we're handling) came */
static DBusConnection* the_connection = NULL;
static DBusConnection* sys_conn = NULL;
DBusConnection* ses_conn = NULL;
static DBusMessage* the_message = NULL;
osso_context_t *osso;


static usb_state_t usb_state = S_INVALID_USB_STATE;
static dbus_uint32_t usb_dialog = -1;
extern gboolean device_locked;
gboolean desktop_started = FALSE;


static GMainLoop *mainloop;


/* Header declarations */
void send_error(const char* s);
static void handle_usb_event(usb_event_t e);



static gboolean set_desktop_started(gpointer data)
{
        ULOG_DEBUG_F("entered");
        desktop_started = TRUE;

#if 0 // MWTODO
        if (!mmc_initialised) {
                /* initialise GConf keys and possibly mount or USB-share */
                if (int_mmc_enabled) {
                        handle_event(E_INIT_CARD, &int_mmc, NULL);
                }
                handle_event(E_INIT_CARD, &ext_mmc, NULL);
                mmc_initialised = TRUE;
        }
#if 0
        if (delayed_auto_install_check) {
                possibly_start_am();
                delayed_auto_install_check = FALSE;
        }
#endif
#endif
        return FALSE;
}

static void notification_closed(unsigned int id)
{
#if 0 // MWTODO
        if (ext_mmc.dialog_id == id) {
                ext_mmc.dialog_id = -1;
        } else if (ext_mmc.swap_dialog_id == id) {
                ext_mmc.swap_dialog_id = -1;
        } else if (int_mmc_enabled && int_mmc.dialog_id == id) {
                int_mmc.dialog_id = -1;
        } else if (int_mmc_enabled && int_mmc.swap_dialog_id == id) {
                int_mmc.swap_dialog_id = -1;
        } else if (usb_dialog == id) {
#endif
        if (usb_dialog == id) {
                usb_dialog = -1;
        } else {
                ULOG_DEBUG_F("unknown dialog id: %u", id);
        }
}

static void action_invoked(unsigned int id, const char *act)
{
#if 0 // MWTODO
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
#endif
        if (usb_dialog == id) {
                ULOG_DEBUG_F("USB dialog action: '%s'", act);
                handle_usb_event(E_EJECT_CANCELLED);
        } else {
                ULOG_DEBUG_F("unknown dialog id: %u", id);
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
                        ULOG_DEBUG_F("device unlocked signal");
                        device_locked = FALSE;
#if 0   /* TODO: if dialog is still open, we don't need to do anything;
           otherwise, we should use the user's decision (saved) */
                        usb_state_t state = get_usb_state();
                        if (state == S_PERIPHERAL) {
                                /* possibly USB-share cards */
                                handle_usb_event(E_ENTER_PERIPHERAL_MODE);
                        }
#endif
	        }
        }
        handled = TRUE;
    } else if (dbus_message_is_signal(m, MCE_SIGNAL_IF,
                                      MCE_SHUTDOWN_SIG)) {
        ULOG_INFO_L("Shutdown signal from MCE, unmounting and exiting");
        g_main_loop_quit(mainloop);
        handled = TRUE;
    } else if (!desktop_started &&
               dbus_message_is_signal(m, DESKTOP_IF, "ready")) {
        ULOG_DEBUG_F("hildon-desktop registered to system bus");
        g_timeout_add(1000, set_desktop_started, NULL);
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

usb_state_t map_usb_mode(gint usb_mode, gboolean vbus) {
    if (usb_mode == USB_MODE_UNKNOWN) {
        ULOG_ERR_F("'usb_device mode' is UNKNOWN, not changing the state");
        return usb_state;
    } else if ((usb_mode == USB_MODE_A_PERIPHERAL) ||
        (usb_mode == USB_MODE_B_PERIPHERAL)) {
        return S_PERIPHERAL_WAIT;
    } else if ((usb_mode == USB_MODE_A_HOST) ||
               (usb_mode == USB_MODE_B_HOST)) {
        return S_HOST;
    } else if ((usb_mode == USB_MODE_A_IDLE) ||
               (usb_mode == USB_MODE_B_IDLE)) {
        if (vbus) {
            return S_PERIPHERAL_WAIT;
        } else {
            return S_CABLE_DETACHED;
        }
    }

	return usb_state;
}

usb_state_t get_usb_state(void)
{
    gboolean vbus;
    gint usb_mode, supply_mode;
    uh_query_state(&vbus, &usb_mode, &supply_mode);
    return map_usb_mode(usb_mode, vbus);
}

static usb_state_t check_usb_cable(void)
{
        usb_state_t state;

        state = get_usb_state();
        if (state == S_HOST) {
                handle_usb_event(E_ENTER_HOST_MODE);
        } else if (state == S_PERIPHERAL_WAIT) {
#if 0
                if (getenv("TA_IMAGE"))
                        /* in TA image, we don't wait for user's decision */
                        handle_usb_event(E_ENTER_PCSUITE_MODE);
                else
#endif
                /*handle_usb_event(E_ENTER_PERIPHERAL_WAIT_MODE);*/

                /* Just go to PC suite mode right away */
                handle_usb_event(E_ENTER_PCSUITE_MODE);
        } else if (state == S_CABLE_DETACHED) {
                handle_usb_event(E_CABLE_DETACHED);
        }
        return state;
}


void uh_callback(gboolean vbus, gint usb_mode, gint supply_mode, gpointer data) {
    ULOG_WARN_F("uh_callback: mode = %d", usb_mode);
	check_usb_cable();
    //usb_state_t = map_usb_mode(usb_mode);
    // TODO: call appropriate mode change function
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

static DBusHandlerResult enable_pcsuite_handler(DBusConnection *c,
                                                DBusMessage *m,
                                                void *data)
{
        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        handle_usb_event(E_ENTER_PCSUITE_MODE);
        send_reply();
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult enable_charging_handler(DBusConnection *c,
                                                DBusMessage *m,
                                                void *data)
{
        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        handle_usb_event(E_ENTER_CHARGING_MODE);
        send_reply();
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult enable_mass_storage_handler(DBusConnection *c,
                                                     DBusMessage *m,
                                                     void *data)
{
        ULOG_DEBUG_F("entered");
        the_connection = c;
        the_message = m;
        handle_usb_event(E_ENTER_MASS_STORAGE_MODE);
        send_reply();
        /* invalidate */
        the_connection = NULL;
        the_message = NULL;
        return DBUS_HANDLER_RESULT_HANDLED;
}

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

static void handle_usb_event(usb_event_t e)
{
        switch (e) {
                case E_CABLE_DETACHED:
                        set_usb_mode_key("idle");
                        inform_usb_cable_attached(FALSE);
                        if (usb_state == S_HOST) {
                                ULOG_DEBUG_F("E_CABLE_DETACHED in S_HOST");
#if 0 // MWTODO
                                dismantle_usb_mount_timeout();
                                unmount_usb_volumes();
#endif
                        } else if (usb_state == S_MASS_STORAGE || usb_state == S_PCSUITE || usb_state == S_PCSUITE_MASS_STORAGE) {
                                if (usb_state == S_MASS_STORAGE || usb_state == S_PCSUITE_MASS_STORAGE) {
                                        ULOG_DEBUG_F("E_CABLE_DETACHED in"
                                                     " S_MASS_STORAGE");
#if 0 // MWTODO
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
#endif
#if 0 // MWTODO
                                        display_dialog(
                                                MSG_USB_DISCONNECTED);
#endif
                                }
                                if (usb_state == S_PCSUITE || usb_state == S_PCSUITE_MASS_STORAGE) {
                                        ULOG_INFO_F("E_CABLE_DETACHED in S_PCSUITE");
                                        if (!disable_pcsuite()) {
                                                ULOG_ERR_F("disable_pcsuite() failed");
                                        }
                                }
                        } else if (usb_state == S_EJECTED) {
                                ULOG_DEBUG_F("E_CABLE_DETACHED in S_EJECTED");
                        } else if (usb_state == S_EJECTING) {
                                ULOG_INFO_F("E_CABLE_DETACHED in S_EJECTING");
#if 0 // MWTODO
                                dismantle_usb_unmount_pending();
                                unmount_usb_volumes();
#endif
                        } else if (usb_state == S_PERIPHERAL_WAIT) {
                                ULOG_INFO_F("E_CABLE_DETACHED in "
                                            "S_PERIPHERAL_WAIT");
                                /* in case e_plugged_helper failed when we
                                 * tried enabling mass storage, we need to
                                 * remount the cards */
                                usb_state = S_MASS_STORAGE;
#if 0 // MWTODO
                                handle_event(E_DETACHED, &ext_mmc, NULL);
                                if (int_mmc_enabled) {
                                        handle_event(E_DETACHED, &int_mmc,
                                                     NULL);
                                }
#endif
                        } else if (usb_state == S_CHARGING) {
                                ULOG_INFO_F("E_CABLE_DETACHED in "
                                            "S_CHARGING");
                        } else {
                                ULOG_WARN_F("E_CABLE_DETACHED in %d!",
                                            usb_state);
                        }
                        usb_state = S_CABLE_DETACHED;
                        break;
                case E_EJECT:
                        if (usb_state == S_HOST) {
                                ULOG_DEBUG_F("E_EJECT in S_HOST");
#if 0 // MWTODO
                                dismantle_usb_mount_timeout();
#endif
                                send_reply();
#if 0 // MWTODO
                                usb_state = try_eject();
#endif
                        } else if (usb_state == S_EJECTING) {
                                ULOG_DEBUG_F("E_EJECT in S_EJECTING");
                                send_reply();
#if 0 // MWTODO
                                if (usb_pending_timer_id == 0) {
                                        usb_state = try_eject();
                                } else {
                                        ULOG_DEBUG_F(
                                            "polling already set up");
                                }
#endif
                        } else {
                                ULOG_WARN_F("E_EJECT in %d", usb_state);
                                send_error("improper state");
                        }
                        break;
                case E_EJECT_CANCELLED:
                        if (usb_state == S_EJECTING) {
                                ULOG_DEBUG_F("E_EJECT_CANCELLED in"
                                             " S_EJECTING");
#if 0 // MWTODO
                                dismantle_usb_unmount_pending();
#endif
                                send_reply();

                                /* possibly re-mount already unmounted
                                 * volumes */
#if 0 // MW TODO
                                mount_usb_volumes();
#endif

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
#if 0 // MWTODO
                                setup_usb_mount_timeout(15);
#endif
                                usb_state = S_HOST;
                        } else {
                                ULOG_WARN_F("E_ENTER_HOST_MODE in %d!",
                                            usb_state);
                        }
                        break;
                case E_ENTER_PERIPHERAL_WAIT_MODE:
#if 0 // MWTODO
                        /* clear the name */
                        free(usb_device_name);
                        usb_device_name = NULL;
                        update_usb_device_name_key();
#endif

                        set_usb_mode_key("peripheral");
                        inform_usb_cable_attached(TRUE);
                        if (usb_state == S_CABLE_DETACHED ||
                            usb_state == S_PERIPHERAL_WAIT) {
                                /* we could be in S_PERIPHERAL_WAIT already
                                 * because of the device lock */
                                ULOG_DEBUG_F("E_ENTER_PERIPHERAL_WAIT_MODE"
                                             " in S_CABLE_DETACHED or "
                                             "S_PERIPHERAL_WAIT");
                                usb_state = S_PERIPHERAL_WAIT;
                        } else {
                                ULOG_WARN_F("E_ENTER_PERIPHERAL_WAIT_MODE"
                                            " in %d!", usb_state);
                        }
                        break;
                case E_ENTER_MASS_STORAGE_MODE:
                        if (usb_state == S_PERIPHERAL_WAIT ||
                            usb_state == S_CHARGING ||
                            usb_state == S_PCSUITE) {
                                usb_state_t orig = usb_state;
                                if (usb_state == S_PCSUITE)
                                        usb_state = S_PCSUITE_MASS_STORAGE;
                                else
                                        usb_state = S_MASS_STORAGE;
#if 0 // MWTODO: this actually sets up mass storage
                                if (!e_plugged_helper()) {
#endif
                                if (FALSE) {
                                        ULOG_DEBUG_F("no card was USB shared"
                                                     " or cable disconnected");
                                        /* no real state change if no card was
                                         * successful */
                                        usb_state = orig;
                                }
                        } else {
                                ULOG_WARN_F("E_ENTER_MASS_STORAGE_MODE in %d!",
                                            usb_state);
                        }
                        break;
                case E_ENTER_PCSUITE_MODE:
                        if (usb_state == S_PERIPHERAL_WAIT ||
                            usb_state == S_CHARGING ||
                            usb_state == S_CABLE_DETACHED ||
                            usb_state == S_MASS_STORAGE) {
                                if (usb_state == S_MASS_STORAGE)
                                        usb_state = S_PCSUITE_MASS_STORAGE;
                                else
                                        usb_state = S_PCSUITE;
                                if (!enable_pcsuite()) {
                                        ULOG_ERR_F("Couldn't enable PC Suite");
                                }
                        } else {
                                ULOG_WARN_F("E_ENTER_PCSUITE_MODE in %d!",
                                            usb_state);
                        }
                        break;
                case E_ENTER_CHARGING_MODE:
                        if (usb_state == S_PERIPHERAL_WAIT) {
                                usb_state = S_CHARGING;
                        } else {
                                ULOG_WARN_F("E_ENTER_CHARGING_MODE in %d!",
                                            usb_state);
                        }
                        break;
                default:
                        ULOG_ERR_F("unknown event %d", e);
        }
}

static void init_slide_keyboard_state()
{
        int state = TRUE;

        /* TODO: set up listener for input event file(s), look at mce */
#if 0 // MWTODO
        if (slide_keyboard_udi != NULL) {
                state = get_prop_bool(slide_keyboard_udi,
                                      "button.state.value");
        }
#endif
        if (state != -1)
                inform_slide_keyboard(state);
}

static gboolean init_usb_cable_status(gpointer data)
{
    check_usb_cable();
    usb_state = get_usb_state();
    return TRUE;
}

static void sigterm(int signo)
{
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

        if (signal(SIGTERM, sigterm) == SIG_ERR) {
                ULOG_CRIT_L("signal() failed");
        }

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

        conn = (DBusConnection*) osso_get_sys_dbus_connection(osso);
        if (conn == NULL) {
                ULOG_CRIT_L("Failed to get system bus connection");
                exit(1);
        }
        sys_conn = conn;

        do_global_init();

        if (!dbus_connection_add_filter(conn, sig_handler, NULL, NULL)) {
                ULOG_CRIT_L("Failed to register signal handler callback");
	        exit(1);
        }

        dbus_bus_add_match(conn, MCE_DEVICELOCK_SIG_MATCH_RULE, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_CRIT_L("dbus_bus_add_match for %s failed",
                            MCE_DEVICELOCK_SIG_MATCH_RULE);
	        exit(1);
        }
        dbus_bus_add_match(conn, MCE_SHUTDOWN_SIG_MATCH_RULE, &error);
        if (dbus_error_is_set(&error)) {
                ULOG_CRIT_L("dbus_bus_add_match for %s failed",
                            MCE_SHUTDOWN_SIG_MATCH_RULE);
	        exit(1);
        }
        /* match for HD readiness signal */
        dbus_bus_add_match(conn, "type='signal',member='ready',"
                           "interface='" DESKTOP_IF "'",
                           &error);
        if (dbus_error_is_set(&error)) {
                ULOG_CRIT_L("dbus_bus_add_match failed");
	        exit(1);
        }

        /* D-Bus interface for 'ejecting' USB mass storages */
        vtable.message_function = eject_handler;
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/usb_eject", NULL);

        /* D-Bus interface for cancelling 'ejecting' of USB mass storages */
        vtable.message_function = cancel_eject_handler;
        register_op(sys_conn, &vtable,
                    "/com/nokia/ke_recv/usb_cancel_eject", NULL);

        /* D-Bus interface for PC suite selection */
        vtable.message_function = enable_pcsuite_handler;
        register_op(sys_conn, &vtable, ENABLE_PCSUITE_OP, NULL);

        /* D-Bus interface for USB mass storage mode selection */
        vtable.message_function = enable_mass_storage_handler;
        register_op(sys_conn, &vtable, ENABLE_MASS_STORAGE_OP, NULL);

        /* D-Bus interface for charging mode selection */
        vtable.message_function = enable_charging_handler;
        register_op(sys_conn, &vtable, ENABLE_CHARGING_OP, NULL);

        if (uh_init() != 0) {
            ULOG_CRIT_L("uh_init() failed");
	        exit(1);
        }

        init_slide_keyboard_state();
        init_usb_cable_status(NULL);

        uh_set_callback((UhCallback)uh_callback, NULL);

        g_main_loop_run(mainloop);
        ULOG_DEBUG_L("Returned from the main loop");

        exit(0);
}
