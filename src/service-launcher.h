/**
  @file service-launcher.h

  This file is part of ke-recv.

  Copyright (C) 2004-2006 Nokia Corporation. All rights reserved.

  Contact: Zeeshan Ali <zeeshan.ali@nokia.com>

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

#ifndef __SERVICE_LAUNCHER_H__
#define __SERVICE_LAUNCHER_H__

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

typedef struct _ServiceLauncher ServiceLauncher;

struct _ServiceLauncher
{
    DBusGConnection *connection;
    DBusGProxy *proxy;
    GConfClient *client;
};

void service_launcher_init (ServiceLauncher * service_launcher);
void service_launcher_deinit (ServiceLauncher * service_launcher);
void service_launcher_launch_services (ServiceLauncher * service_launcher);
gboolean service_launcher_is_authorized (ServiceLauncher * service_launcher);

#endif /* __SERVICE_LAUNCHER_H__ */
