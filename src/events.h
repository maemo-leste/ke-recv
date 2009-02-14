/**
  @file events.h
  
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

#ifndef EVENTS_H_
#define EVENTS_H_

#include <glib.h>
#include <gconf/gconf-client.h>
#include "ke-recv.h"
#include "exec-func.h"
#include "gui.h"
#include "fat-tools.h"

#ifdef __cplusplus
extern "C" {
#endif

/* how often to poll whether MMC has open files */
#define	MMC_USE_RECHECK_TIMEOUT 1000

/* max. seconds to wait for RAM after the exit signal */
#define RAM_WAITING_TIMEOUT 20

#define	MMC_PRESENT_KEY "/system/osso/af/mmc-device-present"
#define	INTERNAL_MMC_PRESENT_KEY "/system/osso/af/internal-mmc-device-present"
#define	MMC_USED_OVER_USB_KEY "/system/osso/af/mmc-used-over-usb"
#define	INTERNAL_MMC_USED_OVER_USB_KEY \
        "/system/osso/af/internal-mmc-used-over-usb"
#define	USB_CABLE_ATTACHED_KEY "/system/osso/af/usb-cable-attached"
#define	MMC_COVER_OPEN_KEY "/system/osso/af/mmc-cover-open"
#define	INTERNAL_MMC_COVER_OPEN_KEY \
        "/system/osso/af/internal-mmc-cover-open"
#define MMC_CORRUPTED_KEY "/system/osso/af/mmc/mmc-corrupted"
#define INTERNAL_MMC_CORRUPTED_KEY "/system/osso/af/mmc/internal-mmc-corrupted"
#define MMC_SWAP_ENABLED_KEY "/system/osso/af/mmc-swap-enabled"
#define CAMERA_OUT_KEY "/system/osso/af/camera-is-out"
#define CAMERA_TURNED_KEY "/system/osso/af/camera-has-turned"
#define SLIDE_OPEN_KEY "/system/osso/af/slide-open"

#define PRE_UNMOUNT_SIGNAL_PROGRAM "/usr/bin/mmc-pre-unmount"
#define MMC_RENAME_PROG "/usr/sbin/mmc-rename.sh"
#define MMC_FORMAT_PROG "/usr/sbin/mmc-format"

typedef enum {
        E_CLOSED,
        E_OPENED,
        E_PLUGGED,
        E_DETACHED,
        E_RENAME,
        E_FORMAT,
        E_REPAIR,
        E_UNMOUNT_TIMEOUT,
       	E_VOLUME_ADDED,
       	E_VOLUME_REMOVED,
       	E_DEVICE_ADDED,
       	E_DEVICE_REMOVED,
        E_ENABLE_SWAP,
        E_DISABLE_SWAP,
        E_INIT_CARD
} mmc_event_t;

/** the suggested volume label has invalid characters */
#define RENAME_ERROR_INVALID_CHARS "invalid_characters"
/** the suggested volume label is too long */
#define RENAME_ERROR_TOO_LONG_NAME "too_long_name"
/** rename request was received while in a state that does not
  allow renaming */
#define RENAME_ERROR_IMPROPER_STATE "improper_state"
/** after renaming the MMC could not be mounted and now the MMC
   is considered corrupt */
#define RENAME_ERROR_MMC_CORRUPTED "mmc_corrupted"
/** could not unmount the MMC */
#define RENAME_ERROR_IN_USE "in_use"

/** MMC was formatted ok but USB hijacked it */
#define FORMAT_MMC_IN_USB_USE "success_usb"
/** the suggested volume label has invalid characters */
#define FORMAT_ERROR_INVALID_CHARS "invalid_characters"
/** the suggested volume label is too long */
#define FORMAT_ERROR_TOO_LONG_NAME "too_long_name"
/** the MMC cover was opened during formatting and now the MMC
   is considered corrupt */
#define FORMAT_ERROR_COVER_OPENED "cover_opened"
/** the use canceled the operation using the Stop button */
#define FORMAT_ERROR_USER_CANCELED "canceled"
/** when the operation couldn't be started or terminated prematurely */
#define FORMAT_ERROR_FORMAT_FAILED "format_failed"
/** format request was received while in a state that does not
  allow formatting */
#define FORMAT_ERROR_IMPROPER_STATE "improper_state"
/** after formatting the MMC could not be mounted */
#define FORMAT_ERROR_MMC_CORRUPTED "mmc_corrupted"
/** could not unmount the MMC */
#define FORMAT_ERROR_IN_USE "in_use"


/* some UI strings */
#define MSG_DEVICE_CONNECTED_VIA_USB _("card_connected_via_usb")
#define MSG_USB_DISCONNECTED _("card_ib_usb_disconnected")
#define MSG_NO_MEMORY_CARD_INSERTED _("card_ni_usb_no_memory_card_inserted")
#define MSG_MEMORY_CARD_AVAILABLE _("card_ib_memory_card_available")
#define MSG_MEMORY_CARD_IS_CORRUPTED_INT _("card_ib_unknown_format_device")
#define MSG_MEMORY_CARD_IS_CORRUPTED _("card_ia_corrupted")
#define MSG_FORMATTING_COMPLETE _("card_ib_formatting_complete")
#define MSG_USB_MEMORY_CARD_IN_USE _("card_ni_usb_failed_card_in_use")
#define MSG_USB_MEMORY_CARDS_IN_USE _("card_ni_usb_failed_cards_in_use")
#define MSG_UNMOUNT_MEMORY_CARD_IN_USE _("card_ni_card_in_use_warning")

#define MSG_CARD_IS_READ_ONLY _("mmc_ib_mmc_is_readonly")
#define MSG_SWAP_CARD_IN_USE _("card_no_mmc_cover_open_mmc_swap")
#define MSG_SWAP_CLOSEAPPS_BUTTON _("card_bd_mmc_cover_open_mmc_swap_ok")
#define MSG_SWAP_IN_USB_USE _("card_no_usb_connected_swap_on")
#define MSG_SWAP_FILE_CORRUPTED _("memr_ni_swap_file_corrupted")
#define MSG_SWAP_USB_CLOSEAPPS_BUTTON \
              _("card_bd_usb_connected_swap_on_closeapps")

#define MAX_MSG_LEN 500

int handle_event(mmc_event_t e, mmc_info_t *mmc, const char *arg);
void do_global_init(void);
int unmount_volumes(volume_list_t *v);
void inform_camera_out(gboolean value);
void inform_camera_turned_out(gboolean value);
void inform_slide_keyboard(gboolean value);
void inform_usb_cable_attached(gboolean value);
void unshare_usb_shared_card(mmc_info_t *mmc);
void show_usb_sharing_failed_dialog(mmc_info_t *in, mmc_info_t *ex);
void set_mmc_corrupted_flag(gboolean value, const mmc_info_t *mmc);
void update_mmc_label(mmc_info_t *mmc);
void possibly_turn_swap_off_simple(mmc_info_t *mmc);

#ifdef __cplusplus
}
#endif
#endif /* EVENTS_H_ */
