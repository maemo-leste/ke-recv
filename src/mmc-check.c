/**
  @file mmc-check.c

  dosfsck wrapper.

  Returns: 
  0 on success
  1 on other error 
  2 on child process (e.g. exec) error
  3 or 4 if dosfsck returns 1 or 2 respectively

  This file is part of ke-recv.

  Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.

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

#include <osso-log.h>
#include "ke-recv.h"
#include <gtk/gtk.h>
#include <hildon/hildon-banner.h>
#include <signal.h>

extern char **environ;

static pid_t child_pid = -1;
static int pipe_fd = -1;
static HildonBanner *hildon_banner = NULL;
static int quick_check = 0;

static void sig_handler(int signo)
{
        pid_t ret = -1;
        int child_status = -1;
        assert(signo == SIGCHLD);

        ret = waitpid(child_pid, &child_status, 0);
        if (ret == -1) {
                exit(1);
        }
        if (WIFEXITED(child_status)) {
                if (WEXITSTATUS(child_status) == 1) {
                        exit(3);
                } else if (WEXITSTATUS(child_status) == 2) {
                        exit(4);
                }
                exit(0);
        } else {
                exit(2);
        }
}

/** Executes a program with arguments in a child process.
  (Does not catch the SIGCHLD signal.)
  @param cmd program to execute.
  @param args array of command line arguments; the last element
  must be NULL.
  @return On success, PID of the child; on error, -1.
*/
static int no_wait_exec(const char* cmd, const char* args[])
{
        pid_t pid = -1;
        int fd[2];
        ULOG_DEBUG_F("entered");
        assert(cmd != NULL);
        if (pipe(fd) < 0) {
                ULOG_ERR_F("pipe() failed");
                return -1;
        }
        pid = fork();
        if (pid < 0) {
                ULOG_ERR_F("fork() failed");
                return -1;
        } else if (pid == 0) {
                int ret = -1;
                close(fd[0]);
                if (dup2(fd[1], STDERR_FILENO) != STDERR_FILENO) {
                        ULOG_ERR_F("dup2() failed");
                }
                close(fd[1]);
                ret = execve(cmd, (char** const) args, environ);
                ULOG_ERR_F("execve() returned error: %s",
                           strerror(errno));
                exit(2);
        }
        ULOG_DEBUG_F("after fork (pid=%d)", pid);
        assert(pid > 0);
        child_pid = pid;
        close(fd[1]);  /* close the writing end */
        pipe_fd = fd[0];
        return pid;
}

/** Reads the output of dosfsck */
static gboolean gio_func(GIOChannel* ch, GIOCondition cond, gpointer p)
{
        static gdouble frac = 0;
        gchar *line = NULL;
        GError* err = NULL;
        GIOStatus ret;

        ULOG_DEBUG_F("entered");
        ret = g_io_channel_read_line(ch, &line, NULL, NULL, &err);

        if (ret == G_IO_STATUS_AGAIN) {
                ULOG_DEBUG_F("G_IO_STATUS_AGAIN");
                return TRUE;
        }
        if (ret != G_IO_STATUS_NORMAL) {
                ULOG_DEBUG_F("pipe error or eof");
                return FALSE;
        }

        assert(hildon_banner != NULL);
        frac += (double)0.01;
        ULOG_DEBUG_F("read line: |%s|, frac=%f", line, frac);
        if (frac > 1.0) frac = 1.0;

        hildon_banner_set_fraction(hildon_banner, frac);
        ULOG_DEBUG_F("leaving");

        return TRUE;
}

static gboolean start_repair(const char *dev)
{
        const char* args[] = {"/sbin/dosfsck", NULL, NULL, NULL, NULL, NULL};

        ULOG_DEBUG_F("entered");

        if (quick_check) {
                /* time-limited check */
                args[1] = "-T";
                args[2] = "10";
                args[3] = dev;
        } else {
                /* full check */
                args[1] = "-n";
                args[2] = dev;
        }

        if (no_wait_exec(args[0], args) > 0) {
                if (!quick_check) {
                        /* set up reading from the pipe */
                        GError* err = NULL;
                        GIOChannel* ch = g_io_channel_unix_new(pipe_fd);
                        if (g_io_channel_set_encoding(ch, NULL, &err) !=
                                        G_IO_STATUS_NORMAL) {
                                ULOG_ERR_F("failed to set encoding");
                        }
                        /*
                        g_io_channel_set_buffered(ch, FALSE);
                        */
                        g_io_add_watch(ch, G_IO_IN | G_IO_ERR | G_IO_HUP,
                                       gio_func, NULL);
                }
                return TRUE;
        } else {
                return FALSE;
        }
}

int main(int argc, char* argv[])
{
        struct sigaction sa;

        ULOG_OPEN("mmc-check");
        ULOG_DEBUG_L("entered");

        if (argc != 2 && argc != 3) {
                ULOG_CRIT_L("Usage: mmc-check <device> [-q]");
                exit(1);
        }
        if (argc == 3) {
                quick_check = 1;
        }

        sa.sa_handler = sig_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(SIGCHLD, &sa, NULL) == -1) {
                ULOG_CRIT_L("could not set SIGCHLD handler");
                exit(1);
        }

        if (!quick_check) {
                if (setlocale(LC_ALL, "") == NULL) {
                        ULOG_CRIT_L("could not set locale");
                        exit(1);
                }
                if (bindtextdomain(PACKAGE, LOCALEDIR) == NULL) {
                        ULOG_ERR_L("bindtextdomain() failed");
                }
                if (textdomain(PACKAGE) == NULL) {
                        ULOG_ERR_L("textdomain() failed");
                }

                if (!gtk_init_check(&argc, &argv)) {
                        ULOG_CRIT_L("gtk_init failed");
                        exit(1);
                }

                hildon_banner =
                        (HildonBanner*)hildon_banner_show_progress(NULL, NULL,
                                "repairing");
                hildon_banner_set_fraction(hildon_banner, 0.0);
                gtk_widget_show(GTK_WIDGET(hildon_banner));
        }

        if (start_repair(argv[1])) {
                if (quick_check) {
                        ULOG_DEBUG_L("waiting for child");
                        sleep(20000);  /* FIXME */
                } else {
                        ULOG_DEBUG_L("going to main loop");
                        gtk_main();
                        ULOG_WARN_L("returned from main loop");
                }
                exit(0);
        } else {
                ULOG_CRIT_L("could not start repairing/checking");
                exit(1);
        }
}
