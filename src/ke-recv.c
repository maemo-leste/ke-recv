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
#include <hildon-mime.h>
#include <libgen.h>


// Set in events.c
extern GConfClient* gconfclient;


static GMainLoop *mainloop;


static void init_slide_keyboard_state()
{
        int state = TRUE;

        /* TODO: set up listener for input event file(s), look at mce */
#if 0
        if (slide_keyboard_udi != NULL) {
                state = get_prop_bool(slide_keyboard_udi,
                                      "button.state.value");
        }
#endif
        if (state != -1)
                inform_slide_keyboard(state);
}


static void sigterm(int signo)
{
        g_main_loop_quit(mainloop);
}

/* Does initialisations and goes to the Glib main loop. */
int main(int argc, char* argv[])
{
        if (signal(SIGTERM, sigterm) == SIG_ERR) {
                ULOG_CRIT_L("signal() failed");
        }

        mainloop = g_main_loop_new(NULL, TRUE);
        ULOG_OPEN(APPL_NAME);

        if (setlocale(LC_ALL, "") == NULL) {
	        ULOG_ERR_L("couldn't set locale");
        }
        if (bindtextdomain(PACKAGE, LOCALEDIR) == NULL) {
                ULOG_ERR_L("bindtextdomain() failed");
        }
        if (textdomain(PACKAGE) == NULL) {
      	        ULOG_ERR_L("textdomain() failed");
        }

        do_global_init();
#if 0
        add_prop_watch(slide_keyboard_udi);
#endif
        init_slide_keyboard_state();

        g_main_loop_run(mainloop);
        ULOG_DEBUG_L("Returned from the main loop");
        /*
        prepare_for_shutdown();
        */

        exit(0);
}
