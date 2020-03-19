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

#define PRE_UNMOUNT_SIGNAL_PROGRAM PACKAGE_LIBEXEC_DIR "/mmc-pre-unmount"
#define MMC_RENAME_PROG PACKAGE_LIBEXEC_DIR "/mmc-rename"
#define MMC_FORMAT_PROG PACKAGE_LIBEXEC_DIR "/mmc-format"
#define GREP_PROG "/bin/grep"

typedef enum {
        E_CLOSED,
        E_OPENED,
        E_PLUGGED,
        E_DETACHED,
        E_RENAME,
        E_FORMAT,
        E_REPAIR,
        E_CHECK,
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
#define MSG_USB_DISCONNECTED _("card_ib_usb_disconnected")
#define MSG_MEMORY_CARD_IS_CORRUPTED_INT _("card_ti_corrupted_device")
#define MSG_MEMORY_CARD_IS_CORRUPTED _("card_ti_corrupted_card")
#define MSG_FORMATTING_COMPLETE _("card_ib_formatting_complete")
#define MSG_USB_INT_MEMORY_CARD_IN_USE_NO_EXT \
        _("card_ni_usb_failed_card_in_use")
#define MSG_USB_MEMORY_CARDS_IN_USE _("card_ni_usb_failed_cards_in_use")

#define MAX_MSG_LEN 500

int handle_event(mmc_event_t e, mmc_info_t *mmc, const char *arg);
void do_global_init(void);
int unmount_volumes(mmc_info_t *mmc, gboolean lazy);
void inform_camera_out(gboolean value);
void inform_camera_turned_out(gboolean value);
void inform_slide_keyboard(gboolean value);
void inform_usb_cable_attached(gboolean value);
void unshare_usb_shared_card(mmc_info_t *mmc);
void show_usb_sharing_failed_dialog(mmc_info_t *in, mmc_info_t *ex,
                                    gboolean ext_failed);
void set_mmc_corrupted_flag(gboolean value, const mmc_info_t *mmc);
void update_mmc_label(mmc_info_t *mmc, const char *udi);
void possibly_turn_swap_off_simple(mmc_info_t *mmc);

#ifdef __cplusplus
}
#endif
#endif /* EVENTS_H_ */
