/**
  @file service-launcher.c

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

#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include "ke-recv.h"
#include "camera.h"
#include "service-launcher.h"

#define GCONF_AF_PATH "/system/osso/af"
#define GCONF_KEY_ON_CAMERA_OUT GCONF_AF_PATH "/on-camera-out"
#define GCONF_KEY_LAUNCH_AUTHORIZED GCONF_AF_PATH "/launch-on-camera"

#define DBUS_DESKTOP_SERVICE "com.nokia.hildon-desktop"

/*static gboolean
is_device_unlocked ()
{
    DBusConnection *system_bus;
    gboolean is_unlocked = TRUE;
    const gchar *mode_name;
    DBusMessage *request = NULL;
    DBusMessage *reply = NULL;
    DBusError error;
    
    dbus_error_init (&error);

    system_bus = dbus_bus_get (DBUS_BUS_SYSTEM, &error);

    if (dbus_error_is_set (&error)) {
        g_error ("DBus err: %s, %s", error.name, error.message);
    }

    request = dbus_message_new_method_call (MCE_SERVICE,
                                            MCE_REQUEST_PATH,
                                            MCE_REQUEST_IF,
                                            MCE_DEVLOCK_MODE_GET);
    if (!request) {
        return TRUE;
    }

    reply = dbus_connection_send_with_reply_and_block (system_bus,
                                                       request,
                                                       -1,
                                                       &error);
    dbus_message_unref (request);

    if (dbus_error_is_set (&error)) {
        g_warning ("DBus err: %s, %s", error.name, error.message);

        dbus_error_free (&error);
    }

    else if (!dbus_message_get_args (reply,
                                &error,
                                DBUS_TYPE_STRING,
                                &mode_name,
                                DBUS_TYPE_INVALID)){
        g_warning ("DBus err get_args %s %s",
                   error.name,
                   error.message);
        dbus_error_free (&error);
        dbus_message_unref (reply);
    } 
    
    else {
        is_unlocked = (strcmp (mode_name, MCE_DEVICE_UNLOCKED) == 0);
        dbus_message_unref (reply);
    }

    return is_unlocked;
}*/

gboolean
service_launcher_is_authorized (ServiceLauncher * launcher)
{
    GError *error = NULL;
    GConfValue *value;
    gboolean authorized = FALSE;

    /* First check if the desktop has been loaded */
    if (!dbus_g_proxy_call (launcher->proxy, "NameHasOwner", &error,
                            G_TYPE_STRING, DBUS_DESKTOP_SERVICE, G_TYPE_INVALID,
                            G_TYPE_BOOLEAN, &authorized, G_TYPE_INVALID)) {
            if (error->domain == DBUS_GERROR &&
                error->code == DBUS_GERROR_REMOTE_EXCEPTION) {
                    g_warning ("Caught remote method exception %s: %s",
                               dbus_g_error_get_name (error), error->message);
            }

            else {
                    g_warning ("%s\n", error->message);
            }
            
            g_error_free (error);
            return FALSE;
    }

    value = gconf_client_get (launcher->client,
                              GCONF_KEY_LAUNCH_AUTHORIZED,
                              &error);

    if (value == NULL || error != NULL) {
            g_warning ("Failed to get gconf key %s\n",
                       GCONF_KEY_LAUNCH_AUTHORIZED);
    }

    else {
            authorized &= gconf_value_get_bool (value);
            gconf_value_free (value);
    }

    return authorized;
}

static void
_service_launcher_str_slist_copy_foreach (gpointer data, gpointer user_data)
{
    GSList **lst = (GSList **) user_data;
    GConfValue *value = (GConfValue *) data;
    gchar *str = g_strdup ((gchar *) gconf_value_get_string (value));

    *lst = g_slist_append (*lst, (gpointer) str);
}

static GSList *
_service_launcher_str_slist_copy (GSList * lst)
{
    GSList *new_lst = NULL;

    g_slist_foreach (lst, _service_launcher_str_slist_copy_foreach, &new_lst);
    return new_lst;
}

GSList *
_service_launcher_get_camera_service_names (ServiceLauncher * launcher);
GSList *
_service_launcher_get_camera_service_names (ServiceLauncher * launcher)
{
    GSList *service_names;
    GConfValue *value;
    GError *error = NULL;

    value = gconf_client_get (launcher->client, GCONF_KEY_ON_CAMERA_OUT, &error);
    if (value == NULL || error != NULL) {
            g_warning ("Failed to get gconf key %s\n", GCONF_KEY_ON_CAMERA_OUT);
            service_names = NULL;
    }

    else {
            service_names = _service_launcher_str_slist_copy (
                            gconf_value_get_list (value));
            gconf_value_free (value);
    }

    return service_names;
}

static void
_service_launcher_launch_service (gpointer data, gpointer user_data)
{
    const gchar *service_name = (const gchar *) data;
    ServiceLauncher *launcher = (ServiceLauncher *) user_data;
    GError *error = NULL;
    guint32 ret;

    if (!dbus_g_proxy_call (launcher->proxy, "StartServiceByName", &error,
                            G_TYPE_STRING, service_name, G_TYPE_UINT, 0,
                            G_TYPE_INVALID, G_TYPE_UINT, &ret,
                            G_TYPE_INVALID)) {
            if (error->domain == DBUS_GERROR
                            && error->code == DBUS_GERROR_REMOTE_EXCEPTION) {
                    g_warning ("Caught remote method exception %s: %s",
                                    dbus_g_error_get_name (error),
                                    error->message);
            }
            
            else {
                    g_warning ("%s\n", error->message);
            }
            g_error_free (error);
    }
}

void
service_launcher_launch_services (ServiceLauncher * launcher)
{
    gboolean is_cam_open;
    gboolean device_is_unlocked;

    g_debug ("%s called\n", G_STRFUNC);
    
    is_cam_open = camera_is_open  ();
    device_is_unlocked = !get_device_lock ();
    
    if (!is_cam_open && device_is_unlocked) {
        GSList *service_names;

        g_debug ("%s: trying to start services now\n", G_STRFUNC);
        service_names = _service_launcher_get_camera_service_names (launcher);

        if (service_names == NULL) {
            return;
        }

        else {
            g_slist_foreach (service_names,
                             _service_launcher_launch_service,
                             launcher);
            g_slist_free (service_names);
        }
    }

    else {
        g_debug ("%s: camera is %s\n",
                  G_STRFUNC, is_cam_open? "open": "closed");
        g_debug ("%s: device is %s\n",
                 G_STRFUNC, device_is_unlocked? "unlocked": "locked");
    }
}

void
service_launcher_init (ServiceLauncher * launcher)
{
    GError *error = NULL;

    launcher->connection = dbus_g_bus_get (DBUS_BUS_STARTER, &error);
    if (launcher->connection == NULL)
    {
            g_error ("Failed to open connection to bus: %s\n", error->message);
            g_error_free (error);
    }

    /* Create a proxy object for the "bus driver"
     * (name "org.freedesktop.DBus") */

    launcher->proxy =
            dbus_g_proxy_new_for_name (launcher->connection,
                            DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                            DBUS_INTERFACE_DBUS);

    launcher->client = gconf_client_get_default ();
    if (launcher->client == NULL)
    {
            g_error ("%s\n", "error creating the default gconf client\n");
    }

}

void
service_launcher_deinit (ServiceLauncher * launcher)
{
    g_object_unref (launcher->proxy);
    g_object_unref (G_OBJECT (launcher->client));
}
