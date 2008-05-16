/**
  @file gui.c
  GUI parts of ke-recv.

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
#include "gui.h"

extern gboolean desktop_started;
extern osso_context_t* osso;

void display_dialog(const gchar* s)
{
        osso_return_t ret;
        assert(osso != NULL);

        if (!desktop_started) {
                ULOG_DEBUG_F("do nothing, desktop is not running");
                return;
        }

        ret = osso_system_note_infoprint(osso, s, NULL);
        if (ret == OSSO_INVALID) {
                ULOG_ERR_F("invalid arguments");
        } else if (ret == OSSO_ERROR) {
                ULOG_ERR_F("other error");
        }
}

void display_system_note(const gchar* s)
{
        osso_return_t ret;
	osso_rpc_t retval;
	ULOG_DEBUG_F("entered");
        assert(osso != NULL);
        ret = osso_system_note_dialog(osso, s, OSSO_GN_NOTICE, &retval);
        if (ret == OSSO_INVALID) {
                ULOG_ERR_F("invalid arguments");
        } else if (ret == OSSO_ERROR) {
                ULOG_ERR_F("other error");
        }
        osso_rpc_free_val(&retval);
}
