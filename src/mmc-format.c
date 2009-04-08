/**
  @file mmc-format.c

  MMC formatting program (mkdosfs wrapper).
  Returns: 
  2 on child process (e.g. exec) error
  1 on other error 
  0 on success

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

#include "mmc-format.h"
#include "exec-func.h"
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <linux/fs.h>

extern char **environ;

/* volume label */
const char* mmc_volume_label = NULL;

/* device file names for mounting, renaming, formatting */
const char* mmc_dev_file_without_part = NULL;
const char* mmc_dev_file_with_part = NULL;

static pid_t child_pid = -1;
static int pipe_fd = -1;
static gboolean ignore_child = FALSE;
static HildonBanner *hildon_banner = NULL;

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
                exit(-2);
        }
        ULOG_DEBUG_F("after fork (pid=%d)", pid);
        assert(pid > 0);
        child_pid = pid;
        close(fd[1]);  /* close the writing end */
        pipe_fd = fd[0];
        return pid;
}

/** Reads the hash marks output by mkdosfs */
static gboolean gio_func(GIOChannel* ch, GIOCondition cond, gpointer p)
{
        static gdouble frac = 0;
        static int hash_count = 0;
        gchar buf[100];
        gsize count;
        GError* err = NULL;
        GIOStatus ret;

        ULOG_DEBUG_F("entered");
        ret = g_io_channel_read_chars(ch, buf, 100, &count, &err);
        if (ret != G_IO_STATUS_NORMAL) {
                ULOG_DEBUG_F("pipe error or eof");
                return FALSE;
        }
        buf[count] = '\0';
        ULOG_DEBUG_F("read %d bytes: |%s|", count, buf);
        if (hash_count == 0) {
                if (sscanf(buf, "%d\n", &hash_count) != 1) {
                        ULOG_ERR_F("could not read the hash count");
			/* garbage from pipe, consider formatting complete... */
                	assert(hildon_banner != NULL);
			hildon_banner_set_fraction(hildon_banner, 1.0);
                        return FALSE;
                }
        } else {
                int n = 0;
                char* p = strchr(buf, '#');
                while (p != NULL) {
                        ++n;
                        p = strchr(p + 1, '#');
                }
                frac += (gdouble) (1.0 / hash_count * n);
                if (frac > 1.0) {
                        frac = 1.0;
                }
                assert(hildon_banner != NULL);
                hildon_banner_set_fraction(hildon_banner, frac);
        }
        return TRUE;
}

/* create the partition if it does not exist and zero
 * beginning of it */
static gboolean prepare_partition()
{
        static const char* args[] = {MMC_PARTITIONING_COMMAND, NULL,
                                     NULL};
        int ret = -1;

        args[1] = mmc_dev_file_without_part;
        ignore_child = TRUE;
        ret = exec_prog(MMC_PARTITIONING_COMMAND, args);
        ignore_child = FALSE;

        if (ret == 0) {
                return TRUE;
        } else {
                ULOG_DEBUG_F("exec_prog returned %d", ret);
                return FALSE;
        }
}

/** Starts the formatting.
  @return true on success, false on error.
*/
static gboolean start_format()
{
        int fd, blk_sz, ret;
        char blk_sz_buf[10];
        const char* args[] = {MMC_FORMAT_COMMAND, NULL,
                              "-n", NULL,  /* volume label */
                              "-S", NULL,  /* logical sector size */
                              "-R", "38",  /* reserved sectors */
                              "-s", "128", /* sectors per cluster */
                              NULL};

        ULOG_DEBUG_F("entered");

        if (mmc_dev_file_without_part != NULL) {
                /* prepare partition table */
                if (!prepare_partition()) {
                        return FALSE;
                }
                ULOG_DEBUG_F("partition prepared");
        }

        args[1] = mmc_dev_file_with_part;
        args[3] = mmc_volume_label;

        /* discover the logical sector size */
        fd = open(mmc_dev_file_with_part, O_RDONLY);
        if (fd < 0) {
                ULOG_ERR_F("open() of '%s' failed: %s",
                           mmc_dev_file_with_part, strerror(errno));
                return FALSE;
        }

        ret = ioctl(fd, BLKSSZGET, &blk_sz);
        if (ret < 0) {
                ULOG_ERR_F("ioctl() failed: %s", strerror(errno));
                close(fd);
                return FALSE;
        }
        close(fd);
        ULOG_DEBUG_F("'%s' has block size of %d", mmc_dev_file_with_part,
                     blk_sz);

        ret = snprintf(blk_sz_buf, sizeof(blk_sz_buf), "%d", blk_sz);
        if (ret < 0) {
                ULOG_ERR_F("snprintf() failed");
                return FALSE;
        }
        args[5] = blk_sz_buf;

        if (no_wait_exec(args[0], args) > 0) {
                /* set up reading from the pipe */
                GError* err = NULL;
                GIOChannel* ch = g_io_channel_unix_new(pipe_fd);
                if (g_io_channel_set_encoding(ch, NULL, &err) !=
                                G_IO_STATUS_NORMAL) {
                        ULOG_ERR_F("failed to set encoding");
                }
                g_io_channel_set_buffered(ch, FALSE);
                g_io_add_watch(ch, G_IO_IN | G_IO_ERR | G_IO_HUP,
                               gio_func, NULL);
                return TRUE;
        } else {
                return FALSE;
        }
}

/** Handler for SIGCHLD.
  @param signo not used
*/
static void sig_handler(int signo)
{
        pid_t ret = -1;
        int child_status = -1;
        assert(signo == SIGCHLD);
        if (ignore_child) {
                ULOG_DEBUG_F("child from exec_prog");
                return;
        }
        ULOG_DEBUG_F("child pid=%d", child_pid);
        ret = waitpid(child_pid, &child_status, 0);
        if (ret == -1) {
                ULOG_ERR_F("waitpid() failed");
                exit(1);
        }
        if (WIFEXITED(child_status)) {
                ULOG_DEBUG_F("child returned: %d",
                             WEXITSTATUS(child_status));
                if (WEXITSTATUS(child_status) != 0) {
                        exit(2);
                }
                sync();  /* sync before exit */
                exit(0);
        } else {
                ULOG_WARN_F("child terminated abnormally");
                exit(2);
        }
}

int main(int argc, char* argv[])
{
        struct sigaction sa;

        ULOG_OPEN(MMC_FORMAT_PROG_NAME);
        ULOG_DEBUG_L("entered");

        if (argc != 3 && argc != 4) {
                ULOG_CRIT_L("Usage: %s [<device>] <partition> <volume label>",
                            argv[0]);
                exit(1);
        }
        sa.sa_handler = sig_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(SIGCHLD, &sa, NULL) == -1) {
                ULOG_CRIT_L("could not set SIGCHLD handler");
                exit(1);
        }
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

        if (argc == 4) {
                mmc_dev_file_without_part = argv[1];
                mmc_dev_file_with_part = argv[2];
                mmc_volume_label = argv[3];
        } else {
                mmc_dev_file_with_part = argv[1];
                mmc_volume_label = argv[2];
        }

        if (!gtk_init_check(&argc, &argv)) {
                ULOG_CRIT_L("gtk_init failed");
                exit(1);
        }

        hildon_banner = (HildonBanner*)hildon_banner_show_progress(NULL, NULL,
                                MSG_FORMATTING_MEMORY_CARD);
        gtk_widget_show(GTK_WIDGET(hildon_banner));
        /* show the banner now to give the user some feedback */
        while (gtk_events_pending()) {
                gtk_main_iteration();
        }

        if (start_format()) {
                ULOG_DEBUG_L("going to main loop");
                gtk_main();
                ULOG_WARN_L("returned from main loop");
                sync();  /* sync before exit */
                exit(0);
        } else {
                ULOG_CRIT_L("could not start formatting");
                exit(1);
        }
}
