/**
  @file events.c
  Event handling functions.
  
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

#include "events.h"
#include "swap_mgr.h"
#include <hildon-mime.h>
#include <fcntl.h>
#include <libgen.h>
 

GConfClient* gconfclient;

void do_global_init(void)
{
        gconfclient = gconf_client_get_default();

#if 0
        if (getenv("OSSO_KE_RECV_IGNORE_CABLE") != NULL) {
                ULOG_WARN_F("OSSO_KE_RECV_IGNORE_CABLE "
                            "defined, ignoring the USB cable");
                ignore_cable = TRUE;
        }
        device_locked = get_device_lock();
#endif
}

void inform_slide_keyboard(gboolean value)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, SLIDE_OPEN_KEY,
                                   !value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed: %s",
                           value, err->message);
                g_error_free(err);
        }
}

