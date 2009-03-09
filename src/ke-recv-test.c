/**
  @file ke-recv-test.c
  ke-recv testing program.

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

#include "ke-recv.h"
#include <stdio.h>

static DBusConnection *ses_conn = NULL;
static DBusConnection *sys_conn = NULL;

static void format_mmc(const char *device)
{
	DBusMessage* m = NULL, *reply = NULL;
	dbus_bool_t ret = FALSE;
	DBusError err;
	const char* label = "";

    	ULOG_DEBUG_F("entering");
	dbus_error_init(&err);
        m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/format",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(m != NULL);
        ret = dbus_message_append_args(m, DBUS_TYPE_STRING,
			&device, DBUS_TYPE_INVALID);
    	if (!ret) {
       	   ULOG_CRIT_F("dbus_message_append_args failed");
           exit(1);
        }
	ret = dbus_message_append_args(m, DBUS_TYPE_STRING,
			&label, DBUS_TYPE_INVALID);
    	if (!ret) {
       	   ULOG_CRIT_F("dbus_message_append_args failed");
           exit(1);
        }
	assert(ses_conn != NULL);
	dbus_error_init(&err);
	reply = dbus_connection_send_with_reply_and_block(ses_conn, m,
			20000, &err);
    	if (dbus_error_is_set(&err)) {
       	   ULOG_ERR_F("error reply: %s", err.message);
        } else if (reply == NULL) {
           ULOG_ERR_F("reply was null, error: %s", err.message);
        }
    	ULOG_DEBUG_F("leaving");
}

static void send_device_locked()
{
        DBusMessage* m = NULL;
        dbus_bool_t ret = FALSE;
	const char* mce_locked = MCE_LOCKED_STR;
        m = dbus_message_new_signal(MCE_SIGNAL_OP, MCE_SIGNAL_IF,
                                    MCE_DEVICELOCK_SIG);
        assert(m != NULL && sys_conn != NULL);
        ret = dbus_message_append_args(m, DBUS_TYPE_STRING,
                        &mce_locked, DBUS_TYPE_INVALID);
        if (!ret) {
       	   ULOG_CRIT_F("dbus_message_append_args failed");
           exit(1);
        }
        dbus_connection_send(sys_conn, m, NULL);
}

static void send_device_unlocked()
{
        DBusMessage* m = NULL;
        dbus_bool_t ret = FALSE;
	const char* mce_unlocked = "unlocked";
        m = dbus_message_new_signal(MCE_SIGNAL_OP, MCE_SIGNAL_IF,
                                    MCE_DEVICELOCK_SIG);
        assert(m != NULL && sys_conn != NULL);
        ret = dbus_message_append_args(m, DBUS_TYPE_STRING,
                        &mce_unlocked, DBUS_TYPE_INVALID);
    	if (!ret) {
       	   ULOG_CRIT_F("dbus_message_append_args failed");
           exit(1);
        }
        dbus_connection_send(sys_conn, m, NULL);
}

static void rename_mmc(const char *device)
{
	DBusMessage* m = NULL;
	dbus_bool_t ret = FALSE;
	DBusError err;
	const char* label = "KERECVTEST";

    	ULOG_DEBUG_F("entering");
	dbus_error_init(&err);

        m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/rename",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(m != NULL);
        ret = dbus_message_append_args(m, DBUS_TYPE_STRING,
			&device, DBUS_TYPE_INVALID);
    	if (!ret) {
       	   ULOG_CRIT_F("dbus_message_append_args failed");
           exit(1);
        }
	ret = dbus_message_append_args(m, DBUS_TYPE_STRING,
			&label, DBUS_TYPE_INVALID);
    	if (!ret) {
       	   ULOG_CRIT_F("dbus_message_append_args failed");
           exit(1);
        }

	assert(ses_conn != NULL);
	dbus_connection_send_with_reply_and_block(ses_conn, m, 20000, &err);
    	if (dbus_error_is_set(&err)) {
       	   ULOG_CRIT_F("error reply: %s", err.message);
           exit(1);
        }
    	ULOG_DEBUG_F("leaving");
}

static void send_swap_on(int mode)
{
	DBusMessage* m = NULL, *reply = NULL;
	DBusError err;
    	ULOG_DEBUG_F("entering");
	assert(sys_conn != NULL);
	dbus_error_init(&err);
        if (mode == 'i') {
	  m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/internal_mmc_swap_on",
			"com.nokia.ke_recv.internal_mmc_swap_on",
			"internal_mmc_swap_on");
        } else {
	  m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/mmc_swap_on",
			"com.nokia.ke_recv.mmc_swap_on",
			"mmc_swap_on");
        }
	reply = dbus_connection_send_with_reply_and_block(sys_conn, m,
			20000, &err);
    	if (reply == NULL) {
       	   ULOG_CRIT_F("dbus_connection_send failed: %s", err.message);
           exit(1);
        }
    	ULOG_DEBUG_F("leaving");
}

static void send_enable_pcsuite()
{
	DBusMessage* m = NULL, *reply = NULL;
	DBusError err;
    	ULOG_DEBUG_F("entering");
	assert(sys_conn != NULL);
	dbus_error_init(&err);
	  m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/enable_pcsuite",
			"com.nokia.ke_recv",
			"dummy");
	reply = dbus_connection_send_with_reply_and_block(sys_conn, m,
			20000, &err);
    	if (reply == NULL) {
       	   ULOG_CRIT_F("dbus_connection_send failed: %s", err.message);
           exit(1);
        }
    	ULOG_DEBUG_F("leaving");
}

static void send_enable_charging()
{
	DBusMessage* m = NULL, *reply = NULL;
	DBusError err;
    	ULOG_DEBUG_F("entering");
	assert(sys_conn != NULL);
	dbus_error_init(&err);
	  m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/enable_charging",
			"com.nokia.ke_recv",
			"dummy");
	reply = dbus_connection_send_with_reply_and_block(sys_conn, m,
			20000, &err);
    	if (reply == NULL) {
       	   ULOG_CRIT_F("dbus_connection_send failed: %s", err.message);
           exit(1);
        }
    	ULOG_DEBUG_F("leaving");
}

static void send_enable_mass_storage()
{
	DBusMessage* m = NULL, *reply = NULL;
	DBusError err;
    	ULOG_DEBUG_F("entering");
	assert(sys_conn != NULL);
	dbus_error_init(&err);
	  m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/enable_mass_storage",
			"com.nokia.ke_recv",
			"dummy");
	reply = dbus_connection_send_with_reply_and_block(sys_conn, m,
			20000, &err);
    	if (reply == NULL) {
       	   ULOG_CRIT_F("dbus_connection_send failed: %s", err.message);
           exit(1);
        }
    	ULOG_DEBUG_F("leaving");
}

static void send_swap_off(int mode)
{
	DBusMessage* m = NULL, *reply = NULL;
	DBusError err;
        dbus_bool_t b = TRUE;
    	ULOG_DEBUG_F("entering");
	assert(sys_conn != NULL);
	dbus_error_init(&err);
        if (mode == 'i') {
	  m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/internal_mmc_swap_off",
			"com.nokia.ke_recv.internal_mmc_swap_off",
			"internal_mmc_swap_off");
        } else {
	  m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/mmc_swap_off",
			"com.nokia.ke_recv.mmc_swap_off",
			"mmc_swap_off");
        }
        dbus_message_append_args(m, DBUS_TYPE_BOOLEAN,
                                 &b, DBUS_TYPE_INVALID);
	reply = dbus_connection_send_with_reply_and_block(sys_conn, m,
			20000, &err);
    	if (reply == NULL) {
       	   ULOG_CRIT_F("dbus_connection_send failed: %s", err.message);
           exit(1);
        }
    	ULOG_DEBUG_F("leaving");
}

static void open_bat_cover()
{
	DBusMessage* m = NULL;
	DBusError err;
    	ULOG_DEBUG_F("entering");
	dbus_error_init(&err);
	m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/test_batt_cover_on",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(sys_conn != NULL && m != NULL);
	dbus_connection_send(sys_conn, m, NULL);
    	ULOG_DEBUG_F("leaving");
}

static void attach_usb()
{
	DBusMessage* m = NULL;
	DBusError err;
    	ULOG_DEBUG_F("entering");
	dbus_error_init(&err);
	m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/test_usb_cable_on",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(sys_conn != NULL && m != NULL);
	dbus_connection_send(sys_conn, m, NULL);
    	ULOG_DEBUG_F("leaving");
}

static void close_bat_cover()
{
	DBusMessage* m = NULL;
	DBusError err;
    	ULOG_DEBUG_F("entering");
	dbus_error_init(&err);
	m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/test_batt_cover_off",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(sys_conn != NULL && m != NULL);
	dbus_connection_send(sys_conn, m, NULL);
    	ULOG_DEBUG_F("leaving");
}

static void detach_usb()
{
	DBusMessage* m = NULL;
    	ULOG_DEBUG_F("entering");
	m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/test_usb_cable_off",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(sys_conn != NULL && m != NULL);
	dbus_connection_send(sys_conn, m, NULL);
    	ULOG_DEBUG_F("leaving");
}

static void repair_card(const char *device)
{
	DBusMessage* m = NULL;
    	ULOG_DEBUG_F("entering");
	m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/check_card",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(sys_conn != NULL && m != NULL);
        dbus_message_append_args(m, DBUS_TYPE_STRING,
                                 &device, DBUS_TYPE_INVALID);
	dbus_connection_send(sys_conn, m, NULL);
    	ULOG_DEBUG_F("leaving");
}

static void usb_eject()
{
	DBusMessage* m = NULL;
    	ULOG_DEBUG_F("entering");
	m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/usb_eject",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(sys_conn != NULL && m != NULL);
	dbus_connection_send(sys_conn, m, NULL);
    	ULOG_DEBUG_F("leaving");
}

static void usb_cancel_eject()
{
	DBusMessage* m = NULL;
    	ULOG_DEBUG_F("entering");
	m = dbus_message_new_method_call("com.nokia.ke_recv",
			"/com/nokia/ke_recv/usb_cancel_eject",
			"com.nokia.ke_recv",
			"dummymethodname");
	assert(sys_conn != NULL && m != NULL);
	dbus_connection_send(sys_conn, m, NULL);
    	ULOG_DEBUG_F("leaving");
}

static void init()
{
    DBusError err;
    ULOG_DEBUG_F("entering");
    dbus_error_init(&err);
    ses_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (ses_conn == NULL) {
        ULOG_CRIT("dbus_bus_get() failed: %s", err.message);
        exit(1);
    }
    sys_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (sys_conn == NULL) {
        ULOG_CRIT("dbus_bus_get() failed: %s", err.message);
        exit(1);
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2 && argc != 3) {
            printf("Usage: %s <command> [/dev/mmcblk(1|0)]\n", argv[0]);
            printf("cb - close battery cover signal\n"
                   "ob - open battery cover signal\n"
                   "f - format device <arg>\n"
                   "r - rename device <arg>\n"
                   "l - send device locked signal\n"
                   "u - send device unlocked signal\n"
                   "at - send USB attached signal\n"
                   "de - send USB detached signal\n"
                   "s - swap on (ext-)MMC\n"
                   "si - swap on (int-)MMC\n"
                   "t - swap off (ext-)MMC\n"
                   "ti - swap off (int-)MMC\n"
                   "e - check device <arg>\n"
                   "ej - eject USB\n"
                   "ec - cancel eject USB\n"
                   "p - enable PC Suite\n"
                   "c - enable charging mode\n"
                   "m - enable USB mass storage\n");
            exit(1);
    }
    ULOG_OPEN("ke_recv_test");
    init();
    switch (argv[1][0]) {
	    case 'c':
                if (argv[1][1] == 'b') {
            	        close_bat_cover();
                } else if (argv[1][1] == '\0')
                        send_enable_charging();
	    	break;
	    case 'o':
                if (argv[1][1] == 'b') {
                        open_bat_cover();
                }
		break;
	    case 'f':
		format_mmc(argv[2]);
		break;
	    case 'r':
		rename_mmc(argv[2]);
		break;
            case 'l':
                send_device_locked();
                break;
            case 'u':
                send_device_unlocked();
                break;
            case 's':
                send_swap_on(argv[1][1]);
                break;
            case 't':
                send_swap_off(argv[1][1]);
                break;
            case 'p':
                send_enable_pcsuite();
                break;
            case 'm':
                send_enable_mass_storage();
                break;
            case 'a':
                if (argv[1][1] == 't') {
                        attach_usb();
                }
                break;
            case 'd':
                if (argv[1][1] == 'e') {
                        detach_usb();
                }
                break;
            case 'e':
                if (argv[1][1] == 'j') {
                        usb_eject();
                } else if (argv[1][1] == 'c') {
                        usb_cancel_eject();
                } else {
                        repair_card(argv[2]);
                }
                break;
	    default:
		printf("invalid argument: %c\n", argv[1][0]);
		exit(1);
    }
    dbus_connection_flush(ses_conn);
    dbus_connection_flush(sys_conn);
    sleep(1);
    exit(0);
}
