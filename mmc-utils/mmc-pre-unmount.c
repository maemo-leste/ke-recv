/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
  This file is part of ke-recv.

  Copyright (C) 2005-2007 Nokia Corporation. All rights reserved.

  Contact: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>

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

#include <string.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>

#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define DVD_DAEMON_SERVICE                          "org.gnome.GnomeVFS.Daemon"
#define DVD_DAEMON_OBJECT                           "/org/gnome/GnomeVFS/Daemon"
#define DVD_DAEMON_INTERFACE                        "org.gnome.GnomeVFS.Daemon"
#define DVD_DAEMON_METHOD_EMIT_PRE_UNMOUNT_VOLUME   "EmitPreUnmountVolume"

static void
emit_pre_unmount (DBusConnection *bus_conn, unsigned int id)
{
	DBusMessage *message, *reply;
	
	message = dbus_message_new_method_call (DVD_DAEMON_SERVICE,
						DVD_DAEMON_OBJECT,
						DVD_DAEMON_INTERFACE,
						DVD_DAEMON_METHOD_EMIT_PRE_UNMOUNT_VOLUME);
	dbus_message_append_args (message,
				  DBUS_TYPE_INT32, &id,
				  DBUS_TYPE_INVALID);
	
	reply = dbus_connection_send_with_reply_and_block (bus_conn, 
							   message,
							   -1,
							   NULL);

	dbus_message_unref (message);
	if (reply) {
		dbus_message_unref (reply);
	}
}

static int check_volume (const char *vol)
{
	GnomeVFSVolumeMonitor *monitor;
        GList *l;

	monitor = gnome_vfs_get_volume_monitor ();
        l = gnome_vfs_volume_monitor_get_mounted_volumes (monitor);
        for (; l != NULL; l = l->next) {
                GnomeVFSVolume *v;
                v = l->data;
                if (v != NULL) {
                        char *s;
                        s = gnome_vfs_volume_get_activation_uri (v);
                        if (strstr(s, vol) != NULL) {
                                return 1;
                        }
                }
        }
        return 0;
}

int main (int argc, char **argv)
{
	DBusConnection        *bus_conn;
	const gchar           *mmc;
	GnomeVFSVolumeMonitor *monitor;
	GnomeVFSVolume        *volume;
	unsigned int          id;

	if (!g_thread_supported ()) g_thread_init (NULL);

	bus_conn = dbus_bus_get (DBUS_BUS_SESSION, NULL);
	if (!bus_conn) {
		g_printerr ("Couldn't get session bus.\n");
		return 1;
	}
	
	if (argc == 2) {
		mmc = argv[1];
	} else {
		mmc = g_getenv ("MMC_MOUNTPOINT");
	}

	if (mmc == NULL) {
		g_printerr ("Usage: %s <mountpoint> or set MMC_MOUNTPOINT\n", argv[0]);
		return 1;
	}

	gnome_vfs_init ();

        if (!check_volume (mmc)) {
                g_printerr ("%s is not mounted\n", mmc);
                return 0;
        }
	monitor = gnome_vfs_get_volume_monitor ();

	volume = gnome_vfs_volume_monitor_get_volume_for_path (monitor, mmc);
	if (!volume) {
                g_printerr ("%s: volume not found\n", mmc);
		return 0;
	}

	id = gnome_vfs_volume_get_id (volume);
	emit_pre_unmount (bus_conn, id);
	
	return 0;
}

