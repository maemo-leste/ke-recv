/**
  @file exec-func.h
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

#ifndef EXEC_FUNC_H_
#define EXEC_FUNC_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MMC_MOUNT_COMMAND "/usr/sbin/osso-mmc-mount.sh"
#define MMC_UMOUNT_COMMAND "/usr/sbin/osso-mmc-umount.sh"
#define MMC_CORRUPTED_SCRIPT "/usr/sbin/osso-mmc-corrupted.sh"
#define MMC_NOT_CORRUPTED_SCRIPT "/usr/sbin/osso-mmc-not-corrupted.sh"
#define LOAD_USB_DRIVER_COMMAND "/usr/sbin/osso-usb-mass-storage-enable.sh"
#define UNLOAD_USB_DRIVER_COMMAND "/usr/sbin/osso-usb-mass-storage-disable.sh"
#define USB_DRIVER_IS_USED_COMMAND "/usr/sbin/osso-usb-mass-storage-is-used.sh"
#define ENABLE_PCSUITE_COMMAND "/usr/sbin/pcsuite-enable.sh"
#define DISABLE_PCSUITE_COMMAND "/usr/sbin/pcsuite-disable.sh"

int exec_prog(const char* cmd, const char* args[]);

/**
  Execute umount command.
  @param mmc memory card
  @param whole_card whether the whole card (all partitions)
         should be unmounted.
  @return true on success.
*/
gboolean run_normal_umount(const mmc_info_t *mmc, gboolean whole_card);

/**
  Execute umount with the -l option.
*/
void run_lazy_umount(void);

/**
  Execute mount command.
  @param mmc memory card
  @return true on success.
*/
gboolean run_mount(const mmc_info_t *mmc);

/**
  Load the USB driver for listed devices.
  Logs errors in case of failure.
  @param arg device names in a NULL-terminated array.
  @return true on success.
*/
gboolean load_usb_driver(const char **arg);

/**
  Unload the USB driver for listed devices.
  Logs errors in case of failure.
  @param arg device names in a NULL-terminated array, or NULL to unload
  them all.
  @return true on success.
*/
gboolean unload_usb_driver(const char **arg);

/**
  Enable PC Suite mode.
  Logs errors in case of failure.
  @return true on success.
*/
gboolean enable_pcsuite(void);

/**
  Disable PC Suite mode.
  Logs errors in case of failure.
  @return true on success.
*/
gboolean disable_pcsuite(void);

/**
  @return true if USB driver is used, false otherwise.
*/
gboolean usb_driver_is_used(void);

#ifdef __cplusplus
}
#endif
#endif /* EXEC_FUNC_H_ */

