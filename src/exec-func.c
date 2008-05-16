/**
  @file exec-func.c
  Script/program executing functionality.

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
#include "exec-func.h"

typedef void (*sighandler_t)(int);

extern char** environ;

/* FIXME: space for two arguments only */
static const char* load_args[] = {LOAD_USB_DRIVER_COMMAND,
                                  NULL, NULL, NULL};
static const char* unload_args[] = {UNLOAD_USB_DRIVER_COMMAND,
                                    NULL, NULL, NULL};

/**
 * Execute a command with arguments.
 * @param cmd command to execute
 * @param args NULL-terminated array of arguments
 * @return return code of the command or -1 on fork(), -2 on
 * exec() error, -3 if the child terminated abnormally, and
 * -4 if waitpid() failed.
 * */
int exec_prog(const char* cmd, const char* args[])
{
        pid_t pid = -1, waitrc = -1;
        int status;
        sighandler_t handler;

        /* ignore SIGCHLD temporarily, so that waitpid()
         * below works (strangely SIG_IGN didn't work) */
        handler = signal(SIGCHLD, SIG_DFL);
        if (handler == SIG_ERR) {
                ULOG_ERR_F("signal() failed: %s", strerror(errno));
                return -1;
        }

        pid = fork();
        if (pid < 0) {
                ULOG_ERR_F("fork() failed");
                signal(SIGCHLD, handler);
                return -1;
        } else if (pid == 0) {
                execve(cmd, (char** const) args, environ);
                ULOG_ERR_F("execve() error: %s", strerror(errno));
                exit(-2);
        }
        assert(pid > 0);

waitpid_again:
        waitrc = waitpid(pid, &status, 0);
        if (waitrc == -1) {
                if (errno == EINTR) {
                        /* interrupted by a signal */
                        goto waitpid_again;
                }
                ULOG_ERR_F("waitpid() returned error: %s",
                           strerror(errno));
                signal(SIGCHLD, handler);
                return -4;
        }
        assert(waitrc == pid);
        if (WIFEXITED(status)) {
                signal(SIGCHLD, handler);
                return WEXITSTATUS(status);
        }
        ULOG_ERR_F("child terminated abnormally");
        signal(SIGCHLD, handler);
        return -3;
}

gboolean run_normal_umount(const mmc_info_t *mmc, gboolean whole_card)
{
        const char* umount_args[] = {MMC_UMOUNT_COMMAND, NULL,
                                     NULL, NULL};
        int ret = -1;
        umount_args[1] = mmc->mount_point;
        if (whole_card) {
                umount_args[2] = mmc->whole_device;
        } 

        ret = exec_prog(MMC_UMOUNT_COMMAND, umount_args);
        if (ret == 0) {
                return TRUE;
        } else {
                return FALSE;
        }
}

static gboolean try_mount(const mmc_info_t *mmc)
{
        const char* mount_args[5] = {MMC_MOUNT_COMMAND, NULL, NULL,
                                     NULL, NULL};
        int ret = -1;
        char* part_device = NULL;
        const volume_list_t *l;

        /* find out the device name of the first partition */
        for (l = &mmc->volumes; l != NULL; l = l->next) {
                if (l->udi != NULL && l->volume_number == 1) {
                        part_device = l->dev_name;
                        break;
                }
        }
        if (part_device == NULL) {
                ULOG_ERR_F("device name for first partition not found");
                return FALSE;
        }

        mount_args[1] = part_device;
        mount_args[2] = mmc->whole_device;
        mount_args[3] = mmc->mount_point;
        ret = exec_prog(MMC_MOUNT_COMMAND, mount_args);
        if (ret == 0) {
                return TRUE;
        } else {
                ULOG_DEBUG_F("exec_prog returned %d", ret);
                return FALSE;
        }
}

gboolean run_mount(const mmc_info_t *mmc)
{
        return try_mount(mmc);
}

gboolean load_usb_driver(const char **arg)
{
        int ret = -1, i = 1, j = 0;

        for (; arg[j] != NULL; ++i, ++j) {
                load_args[i] = arg[j];
        }
        load_args[i] = NULL;

        ret = exec_prog(LOAD_USB_DRIVER_COMMAND, load_args);
	if (ret != 0) {
                return FALSE;
	} else {
                return TRUE;
        }
}

gboolean unload_usb_driver(const char **arg)
{
        int ret = -1, i = 1, j = 0;

        for (; arg && arg[j] != NULL; ++i, ++j) {
                unload_args[i] = arg[j];
        }
        unload_args[i] = NULL;

        ret = exec_prog(UNLOAD_USB_DRIVER_COMMAND, unload_args);
	if (ret != 0) {
                return FALSE;
	} else {
                return TRUE;
        }
}

gboolean usb_driver_is_used(void)
{
        static const char *args[] = {USB_DRIVER_IS_USED_COMMAND, NULL};
        int ret;

        ret = exec_prog(args[0], args);
        if (ret < 0) {
                ULOG_ERR_F("exec_prog() failed");
                return FALSE;
        } else if (ret > 0) {
                return TRUE;
	} else {
                return FALSE;
        }
}

