/**
  @file events.c
  Event handling functions.
  
  This file is part of ke-recv.

  Copyright (C) 2004-2009 Nokia Corporation. All rights reserved.

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
#include <libhal.h>

extern DBusConnection *ses_conn;
extern gboolean desktop_started;

/* whether to handle USB cable or not */
static gboolean ignore_cable = FALSE;

/* whether or not the device is locked */
gboolean device_locked = FALSE;

typedef enum { USB_DIALOG, NORMAL_DIALOG, NO_DIALOG } swap_dialog_t;

GConfClient* gconfclient;

static void usb_share_card(mmc_info_t *mmc, gboolean show);
static int mount_volumes(mmc_info_t *mmc, gboolean show_errors);
static int do_unmount(const char *mountpoint);
static void open_dialog_helper(mmc_info_t *mmc);
static volume_list_t *get_nth_volume(mmc_info_t *mmc, int n);

#if 0
#define CLOSE_DIALOG if (mmc->dialog_id == -1) { \
                             ULOG_WARN_F("%s dialog_id is invalid", \
                                         mmc->name); \
                     } else { \
                             close_closeable_dialog(mmc->dialog_id); \
                     }
#endif
#define CLOSE_DIALOG

#define CLOSE_SWAP_DIALOG \
            if (mmc->swap_dialog_id != -1) { \
                ULOG_DEBUG_F("reaping %s dialog %d", mmc->name, \
                             mmc->swap_dialog_id); \
                close_closeable_dialog(mmc->swap_dialog_id); \
                if (mmc->swap_dialog_response != NULL) { \
                        free(mmc->swap_dialog_response); \
                        mmc->swap_dialog_response = NULL; \
                } \
            }

static void emit_gnomevfs_pre_unmount(const char *mount_point)
{
        static const char* args[] = {PRE_UNMOUNT_SIGNAL_PROGRAM,
                                     NULL, NULL};
        args[1] = mount_point;
        int ret = exec_prog(PRE_UNMOUNT_SIGNAL_PROGRAM, args);
        if (ret != 0) {
                ULOG_ERR_F("exec_prog returned %d", ret);
        }
}

static void set_localised_label(mmc_info_t *mmc)
{
        if (mmc->internal_card) {
                strcpy(mmc->display_name,
                       (const char*)dgettext("hildon-fm",
                        "sfil_li_memorycard_internal"));
        } else {
                strcpy(mmc->display_name,
                       (const char*)dgettext("hildon-fm",
                        "sfil_li_memorycard_removable"));
        }
}

static void empty_file(const char *file)
{
        FILE *f;
        f = fopen(file, "w");
        if (f) {
                fclose(f);
        }
}

#define UPDATE_MMC_LABEL_SCRIPT "/usr/sbin/osso-update-mmc-label.sh"

void update_mmc_label(mmc_info_t *mmc)
{
        const char* args[] = {UPDATE_MMC_LABEL_SCRIPT,
                              NULL, NULL, NULL};
        int ret;
        gchar* buf = NULL;
        GError* err = NULL;
        char *part_device = NULL;
        volume_list_t *vol;

        vol = get_nth_volume(mmc, mmc->preferred_volume);
        if (vol == NULL) {
                ULOG_ERR_F("could not find partition number %d",
                           mmc->preferred_volume);
                if (mmc->preferred_volume != 1 && mmc->internal_card) {
                        /* workaround for not yet partitioned systems */
                        ULOG_DEBUG_F("falling back to partition 1!");
                        vol = get_nth_volume(mmc, 1);
                        if (vol == NULL)
                                return;
                } else
                        return;
        }
        part_device = vol->dev_name;

        if (part_device == NULL) {
                ULOG_ERR_F("device name for partition number %d not found",
                           vol->volume_number);
                empty_file(mmc->volume_label_file);
                set_localised_label(mmc);
                return;
        }

        args[1] = part_device;
        args[2] = mmc->volume_label_file;

        ret = exec_prog(UPDATE_MMC_LABEL_SCRIPT, args);
        if (ret != 0) {
                ULOG_ERR_F("exec_prog returned %d", ret);
        }

        /* read the volume label from the file */

        if (!g_file_get_contents(mmc->volume_label_file, &buf, NULL, &err)
            || err != NULL) {
                ULOG_ERR_F("couldn't read volume label file %s",
                           mmc->volume_label_file);
                g_error_free(err);
                set_localised_label(mmc);
        } else {
                if (buf[0] == '\0' || buf[0] == ' ') {
                        set_localised_label(mmc);
                } else {
                        strcpy(mmc->display_name, (const char*)buf);
                }
        }
        g_free(buf);
}

static void inform_device_present(gboolean value, const mmc_info_t *mmc)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, mmc->presence_key,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed for %s: %s",
                           value, mmc->presence_key, err->message);
                g_error_free(err);
        }
}

void inform_usb_cable_attached(gboolean value)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, USB_CABLE_ATTACHED_KEY,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed: %s",
                           value, err->message);
                g_error_free(err);
        }
}

static void inform_mmc_cover_open(gboolean value, const mmc_info_t *mmc)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, mmc->cover_open_key,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed for %s: %s",
                           value, mmc->cover_open_key, err->message);
                g_error_free(err);
        }
}

void inform_camera_out(gboolean value)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, CAMERA_OUT_KEY,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed: %s",
                           value, err->message);
                g_error_free(err);
        }
        if (value) {
                send_systembus_signal("/com/nokia/ke_recv/camera_out",
                                      "com.nokia.ke_recv.camera_out",
                                      "camera_out");
        } else {
                send_systembus_signal("/com/nokia/ke_recv/camera_in",
                                      "com.nokia.ke_recv.camera_in",
                                      "camera_in");
        }
}

void inform_camera_turned_out(gboolean value)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, CAMERA_TURNED_KEY,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed: %s",
                           value, err->message);
                g_error_free(err);
        }
        if (value) {
                send_systembus_signal("/com/nokia/ke_recv/camera_turned_out",
                                      "com.nokia.ke_recv.camera_turned_out",
                                      "camera_turned_out");
        } else {
                send_systembus_signal("/com/nokia/ke_recv/camera_turned_in",
                                      "com.nokia.ke_recv.camera_turned_in",
                                      "camera_turned_in");
        }
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

static void inform_mmc_swapping(gboolean value, const mmc_info_t *mmc)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, mmc->swapping_key,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed for %s: %s",
                           value, mmc->swapping_key, err->message);
                g_error_free(err);
        }
}

void set_mmc_corrupted_flag(gboolean value, const mmc_info_t *mmc)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, mmc->corrupted_key,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed for %s: %s",
                           value, mmc->corrupted_key, err->message);
                g_error_free(err);
        }
}

static void inform_mmc_used_over_usb(gboolean value, const mmc_info_t *mmc)
{
        GError* err = NULL;
        assert(gconfclient != NULL);
        if (!gconf_client_set_bool(gconfclient, mmc->used_over_usb_key,
                                   value, &err) && err != NULL) {
                ULOG_ERR_F("gconf_client_set_bool(%d) failed for %s: %s",
                           value, mmc->used_over_usb_key, err->message);
                g_error_free(err);
        }
}

static void possibly_turn_swap_off(swap_dialog_t, mmc_info_t*);

/** Function called when MMC usage status should be rechecked.
 * If MMC is not in use anymore, moves to S_COVER_OPEN state.
 * @param data not used
 * @return returns TRUE if MMC usage should be checked later. 
 * */
static gboolean unmount_pending_recheck(gpointer data)
{
        mmc_info_t *mmc = data;
        assert(mmc->unmount_pending_timer_id != 0);
        possibly_turn_swap_off(NORMAL_DIALOG, mmc);
        handle_event(E_UNMOUNT_TIMEOUT, mmc, NULL);
#if 0
        if (mmc_in_use(mmc)) {
                /* re-check later */
                ULOG_DEBUG_F("%s is still in use", mmc->name);
                return TRUE;
        } else {
                CLOSE_DIALOG
                CLOSE_SWAP_DIALOG
                mmc->state = S_COVER_OPEN;
                mmc->unmount_pending_timer_id = 0;
                return FALSE;
        }
#endif
        return FALSE;
}

static void setup_s_unmount_pending(mmc_info_t *mmc)
{
        if (mmc->unmount_pending_timer_id) {
                ULOG_WARN_F("timer was already set for %s", mmc->name);
        } else {
                mmc->unmount_pending_timer_id =
                        g_timeout_add(MMC_USE_RECHECK_TIMEOUT,
                                      unmount_pending_recheck, mmc);
        }
}

static void dismantle_s_unmount_pending(mmc_info_t *mmc)
{
        if (mmc->unmount_pending_timer_id) {
                if (!g_source_remove(mmc->unmount_pending_timer_id)) {
                        ULOG_WARN_F("timer did not exist for %s",
                                    mmc->name);
                }
                mmc->unmount_pending_timer_id = 0;
        }
}

static gboolean possibly_turn_swap_on(const mmc_info_t *mmc)
{
        int ret;

        if (mmc->swap_location == NULL) {
                return FALSE;
        }

        if (setenv("OSSO_SWAP", mmc->swap_location, 1)) {
                ULOG_ERR_F("setenv() failed");
                return FALSE;
        }
        
        if (swap_enabled()) {
                ULOG_DEBUG_F("swapping is already on for %s",
                             mmc->name);
                inform_mmc_swapping(TRUE, mmc);
                return TRUE;
        }
        ret = swap_switch_on();
        if (ret != 0) {
                inform_mmc_swapping(FALSE, mmc);
                ULOG_WARN_F("swap_switch_on() for %s: %s", mmc->name,
                            strerror(ret));
                if (ret == EINVAL) {
                        display_system_note(MSG_SWAP_FILE_CORRUPTED);
                }
                return FALSE;
        }
        inform_mmc_swapping(TRUE, mmc);
        return TRUE;
}

static void check_swap_dialog(mmc_info_t *mmc)
{
        gboolean retval;

        ULOG_DEBUG_F("%s dialog (id %u) response was %s",
                     mmc->name, mmc->swap_dialog_id,
                     mmc->swap_dialog_response);

        free(mmc->swap_dialog_response);
        mmc->swap_dialog_response = NULL;

        /* ask TN to close applications */
        retval = send_exit_signal();
        if (retval) {
                int max_wait = RAM_WAITING_TIMEOUT;
                for (; max_wait > 0; --max_wait) {
                        /* wait for more RAM */
                        /* FIXME: should handle events in
                         * the meanwhile */
                        sleep(1);
                        if (swap_can_switch_off()) {
                                break;
                        }
                }
                if (max_wait == 0) {
                        ULOG_ERR_F("Not enough RAM to swapoff "
                                   "after %d s of waiting",
                                   RAM_WAITING_TIMEOUT);
                }
        }
}

static void possibly_turn_swap_off(swap_dialog_t dialog, mmc_info_t *mmc)
{
        if (mmc->swap_location == NULL) {
                return;
        }

        if (setenv("OSSO_SWAP", mmc->swap_location, 1)) {
                ULOG_ERR_F("setenv() failed");
                return;
        }

        if (!swap_enabled()) {
                ULOG_DEBUG_F("swapping is not on for %s", mmc->name);
                if (mmc->swap_dialog_id != -1) {
                        CLOSE_SWAP_DIALOG
                }
                inform_mmc_swapping(FALSE, mmc);
                return;
        }
        if (mmc->swap_dialog_response != NULL) {
                check_swap_dialog(mmc);
        }
        if (mmc->swap_dialog_id != -1 && dialog == NO_DIALOG) {
                CLOSE_SWAP_DIALOG
        }
        if (swap_can_switch_off()) {
                time_t start;
                int ret;
                ULOG_DEBUG_F("before swap_switch_off() for %s", mmc->name);
                start = time(NULL);
                ret = swap_switch_off();
                ULOG_DEBUG_F("swap_switch_off() took %u s",
                             (unsigned)(time(NULL) - start));
                if (ret != 0) {
                        ULOG_WARN_F("swap_switch_off() for %s: %s",
                                    mmc->name, strerror(ret));
                }
                if (!swap_enabled()) {
                        CLOSE_SWAP_DIALOG
                        inform_mmc_swapping(FALSE, mmc);
                }
        } else if (mmc->swap_dialog_id == -1 && dialog != NO_DIALOG) {
                ULOG_DEBUG_F("cannot safely turn %s swap off, new dialog",
                             mmc->name);
                if (dialog == USB_DIALOG) {
                        mmc->swap_dialog_id =
                                open_closeable_dialog(OSSO_GN_WARNING,
                                        MSG_SWAP_IN_USB_USE,
                                        MSG_SWAP_USB_CLOSEAPPS_BUTTON);
                } else {
                        mmc->swap_dialog_id =
                                open_closeable_dialog(OSSO_GN_WARNING,
                                        MSG_SWAP_CARD_IN_USE,
                                        MSG_SWAP_CLOSEAPPS_BUTTON);
                }
        }
}

void possibly_turn_swap_off_simple(mmc_info_t *mmc)
{
        if (mmc->swap_location == NULL) {
                return;
        }

        if (setenv("OSSO_SWAP", mmc->swap_location, 1)) {
                ULOG_ERR_F("setenv() failed");
                return;
        }

        if (!swap_enabled()) {
                ULOG_DEBUG_F("swapping is not on for %s", mmc->name);
                inform_mmc_swapping(FALSE, mmc);
                return;
        }
        if (swap_can_switch_off()) {
                int ret;
                ret = swap_switch_off();
                if (ret != 0) {
                        ULOG_WARN_F("swap_switch_off() for %s: %s",
                                    mmc->name, strerror(ret));
                }
                if (!swap_enabled()) {
                        inform_mmc_swapping(FALSE, mmc);
                }
        } else {
                ULOG_DEBUG_F("cannot safely turn %s swap off", mmc->name);
        }
}

static gboolean possibly_turn_swap_off_nocheck(mmc_info_t *mmc)
{
        int ret;

        if (mmc->swap_location == NULL) {
                return TRUE;
        }

        if (setenv("OSSO_SWAP", mmc->swap_location, 1)) {
                ULOG_ERR_F("setenv() failed");
                return FALSE;
        }

        if (!swap_enabled()) {
                ULOG_DEBUG_F("swapping is not on");
                inform_mmc_swapping(FALSE, mmc);
                return TRUE;
        }
        ret = swap_switch_off();
        if (ret != 0) {
                ULOG_WARN_F("swap_switch_off() for %s: %s", mmc->name,
                            strerror(ret));
                return FALSE;
        }
        inform_mmc_swapping(FALSE, mmc);
        return TRUE;
}

void do_global_init(void)
{
        gconfclient = gconf_client_get_default();

        if (getenv("OSSO_KE_RECV_IGNORE_CABLE") != NULL) {
                ULOG_WARN_F("OSSO_KE_RECV_IGNORE_CABLE "
                            "defined, ignoring the USB cable");
                ignore_cable = TRUE;
        }
        device_locked = get_device_lock();
}

void unshare_usb_shared_card(mmc_info_t *mmc)
{
        const char *args[] = {NULL, NULL}; 
        char *dev;

        if (!mmc->control_partitions) {
                volume_list_t *vol;

                vol = get_nth_volume(mmc, mmc->preferred_volume);
                if (vol == NULL) {
                        if (mmc->preferred_volume != 1) {
                                /* workaround for not yet partitioned systems */
                                ULOG_DEBUG_F("falling back to partition 1!");
                                vol = get_nth_volume(mmc, 1);
                                if (vol == NULL || vol->dev_name == NULL)
                                        return;
                        } else
                                return;
                }
                dev = vol->dev_name;
        } else
                dev = mmc->whole_device;

        if (dev == NULL) {
                ULOG_DEBUG_F("dev unknown for %s", mmc->name);
                return;
        }

        args[0] = dev;
        if (!unload_usb_driver(args)) {
                ULOG_ERR_F("failed to unload the USB module for %s", dev);
                /* there seems to be no way to recover from this... */
        } else {
                inform_mmc_used_over_usb(FALSE, mmc);
        }
}

static void usb_share_card(mmc_info_t *mmc, gboolean show)
{
        const char *args[] = {NULL, NULL};
        char *dev;

        if (!mmc->control_partitions) {
                volume_list_t *vol;

                vol = get_nth_volume(mmc, mmc->preferred_volume);
                if (vol == NULL) {
                        ULOG_ERR_F("volume %d not found from %s",
                                   mmc->preferred_volume, mmc->name);
                        if (mmc->preferred_volume != 1) {
                                /* workaround for not yet partitioned systems */
                                ULOG_DEBUG_F("falling back to partition 1!");
                                vol = get_nth_volume(mmc, 1);
                                if (vol == NULL || vol->dev_name == NULL)
                                        return;
                        } else
                                return;
                }
                dev = vol->dev_name;
        } else
                dev = mmc->whole_device;

        if (dev == NULL) {
                ULOG_DEBUG_F("dev unknown for %s", mmc->name);
                return;
        }

        if (mmc->storage_udi == NULL) {
                ULOG_DEBUG_F("device is not ready yet");
                return;
        }

        args[0] = dev;
        if (load_usb_driver(args)) {
                ULOG_INFO_F("USB mass storage module loaded for %s", dev);
                /*
                if (show) {
                        display_dialog(MSG_DEVICE_CONNECTED_VIA_USB);
                }
                */
                inform_mmc_used_over_usb(TRUE, mmc);
        } else {
                ULOG_ERR_F("failed to load USB mass storage module for %s",
                           dev);
        }
}

static void handle_e_rename(mmc_info_t *mmc)
{
        const char* args[] = {MMC_RENAME_PROG, NULL, NULL, NULL};
        int ret;
        volume_list_t *vol;

        vol = get_nth_volume(mmc, mmc->preferred_volume);

        if (vol == NULL || vol->dev_name == NULL) {
                ULOG_ERR_F("could not find partition number %d",
                           mmc->preferred_volume);
                if (mmc->preferred_volume != 1 && mmc->internal_card) {
                        /* workaround for not yet partitioned systems */
                        ULOG_DEBUG_F("falling back to partition 1!");
                        vol = get_nth_volume(mmc, 1);
                        if (vol == NULL || vol->dev_name == NULL)
                                return;
                } else
                        return;
        }

        ULOG_DEBUG_F("using device file %s", vol->dev_name);
        args[1] = vol->dev_name;
        args[2] = mmc->desired_label;

        /* check validity of volume label */
        ret = valid_fat_name(args[2]);
        assert(ret < 1 && ret > -3);
        if (ret == -1) {
                ULOG_ERR_F("too long name");
                return;
        } else if (ret == -2) {
                ULOG_ERR_F("invalid characters");
                return;
        }

        /* Currently renaming is done to unmounted memory card. This would
         * not be necessary, but there is a problem of updating the volume
         * label otherwise (sending GnomeVFS signals didn't work). */
        if (do_unmount(mmc->mount_point)) {
                ret = exec_prog(args[0], args);
                if (ret != 0) {
                        ULOG_ERR_F("mlabel failed: exec_prog returned %d",
                                   ret);
                        if (mmc->internal_card)
                                open_closeable_dialog(OSSO_GN_NOTICE,
                                    MSG_MEMORY_CARD_IS_CORRUPTED_INT, "OMG!");
                        else
                                open_closeable_dialog(OSSO_GN_NOTICE,
                                    MSG_MEMORY_CARD_IS_CORRUPTED, "OMG!");
                        /* even if renaming failed it makes sense to
                         * try mounting the card */
                }

                update_mmc_label(mmc);
                ULOG_DEBUG_F("successful renaming");
                if (!mount_volumes(mmc, TRUE)) {
                        ULOG_ERR_F("could not mount it");
                }
        } else {
                ULOG_DEBUG_F("umount failed");
                display_system_note(dgettext("osso-filemanager",
                                    "sfil_ni_mmc_rename_mmc_in_use"));
        }
}

static volume_list_t *get_nth_volume(mmc_info_t *mmc, int n)
{
        volume_list_t *l, *ret = NULL;

        for (l = &mmc->volumes; l != NULL; l = l->next) {
                if (l->udi != NULL && l->volume_number == n) {
                        ret = l;
                        break;
                }
        }
        return ret;
}

static void handle_e_format(mmc_info_t *mmc)
{
        int ret;
        const char* args[] = {MMC_FORMAT_PROG, NULL, NULL, NULL, NULL};
        volume_list_t *vol;
        char buf[100];

        ULOG_DEBUG_F("label for %s is '%s'", mmc->name, mmc->desired_label);

        vol = get_nth_volume(mmc, mmc->preferred_volume);

        if ((vol == NULL || vol->dev_name == NULL)
            && !mmc->control_partitions) {
                ULOG_ERR_F("could not find partition number %d",
                           mmc->preferred_volume);
                if (mmc->preferred_volume != 1 && mmc->internal_card) {
                        /* workaround for not yet partitioned systems */
                        ULOG_DEBUG_F("falling back to partition 1!");
                        vol = get_nth_volume(mmc, 1);
                        if (vol == NULL || vol->dev_name == NULL)
                                return;
                } else
                        return;
        }

        if (!mmc->control_partitions) {
                args[1] = vol->dev_name;
                args[2] = mmc->desired_label;
                ULOG_DEBUG_F("using device file %s", args[1]);

                if (vol->mountpoint != NULL)
                        ret = do_unmount(vol->mountpoint);
                else {
                        ULOG_DEBUG_F("no mountpoint, using dev_name");
                        ret = do_unmount(vol->dev_name);
                }
        } else {
                args[1] = mmc->whole_device;
                if (vol == NULL || vol->dev_name == NULL) {
                        snprintf(buf, 100, "%sp%d", mmc->whole_device,
                                 mmc->preferred_volume);
                } else
                        snprintf(buf, 100, "%s", vol->dev_name);
                args[2] = buf;
                args[3] = mmc->desired_label;

                ret = unmount_volumes(mmc);
        }
        if (!ret) {
                ULOG_INFO_F("memory card %s is in use", mmc->name);
                display_system_note(dgettext("osso-filemanager",
                                    "sfil_ni_mmc_format_mmc_in_use"));
                if (mmc->control_partitions)
                        /* we could have unmounted some other volumes */
                        mount_volumes(mmc, TRUE);
                return;
        }

        if (mmc->control_partitions)
                /* partition table will be cleared, clear volume info */
                clear_volume_list(&mmc->volumes);

        ULOG_DEBUG_F("execing: %s %s %s %s", args[0], args[1], args[2],
                     args[3]);
        ret = exec_prog(args[0], args);
        if (ret != 0) {
                ULOG_INFO_F("format of %s failed, rc=%d", mmc->name, ret);
        } else {
                /* mount the newly formatted volume if we did not re-partition
                 * the device; otherwise it is mounted later when the new
                 * partition table is discovered */
                if (!mmc->control_partitions) {
                        mount_volumes(mmc, TRUE);
                } else {
                        mmc->skip_banner = TRUE;
                }
                display_dialog(MSG_FORMATTING_COMPLETE);
        }
}

#define MSG_UNABLE_TO_REPAIR _("card_unable_to_repair_memory_card")

static void handle_e_repair(mmc_info_t *mmc)
{
        int ret, mounted;
        char *part_device = NULL, *udi = NULL;
        volume_list_t *l;
        const char* args[] = {"/usr/sbin/mmc-check", NULL, NULL};

        l = get_nth_volume(mmc, mmc->preferred_volume);

        if (l && l->dev_name && l->udi) {
                part_device = l->dev_name;
                udi = l->udi;
        } else {
                ULOG_ERR_F("device name for the partition not found");
                display_system_note(MSG_UNABLE_TO_REPAIR);
                return;
        }

        /* check if someone else has already mounted it */
        mounted = get_prop_bool(udi, "volume.is_mounted");
        if (mounted) {
                ULOG_INFO_F("%s is mounted", part_device);
                return;
        }

        args[1] = part_device;

        /* exec mmc-check */
        ret = exec_prog(args[0], args);

        if (ret > 2) {
                ULOG_ERR_F("dosfsck returned: %d", ret - 2);
                display_system_note(MSG_UNABLE_TO_REPAIR);
                return;
        } else if (ret > 0) {
                ULOG_ERR_F("mmc-check error code: %d", ret);
                display_system_note(MSG_UNABLE_TO_REPAIR);
                return;
        } else if (ret < 0) {
                ULOG_ERR_F("exec_prog error code: %d", ret);
                display_system_note(MSG_UNABLE_TO_REPAIR);
                return;
        }
        l->corrupt = 0;
        set_mmc_corrupted_flag(FALSE, mmc);

        init_mmc_volumes(mmc); /* re-init volumes */
        if (mount_volumes(mmc, FALSE)) {
                display_system_note(_("card_memory_card_repaired"));
        } else {
                display_system_note(MSG_UNABLE_TO_REPAIR);
        }
}

static void handle_e_check(mmc_info_t *mmc)
{
        int ret, mounted;
        char *part_device = NULL, *udi = NULL;
        volume_list_t *l;
        const char* args[] = {"/usr/sbin/mmc-check", NULL, NULL};

        l = get_nth_volume(mmc, mmc->preferred_volume);

        if (l && l->dev_name && l->udi) {
                part_device = l->dev_name;
                udi = l->udi;
        } else {
                ULOG_ERR_F("device name for the partition not found");
                display_system_note("Partition not found");
                return;
        }

        /* check if someone else has already mounted it */
        mounted = get_prop_bool(udi, "volume.is_mounted");
        if (mounted) {
                ULOG_INFO_F("%s is mounted", part_device);
                return;
        }

        args[1] = part_device;

        /* exec mmc-check */
        ret = exec_prog(args[0], args);

        if (ret > 2) {
                ULOG_ERR_F("dosfsck returned: %d", ret - 2);
                display_system_note("dosfsck failed");
                /* TODO: set corrupt flag */
                return;
        } else if (ret > 0) {
                ULOG_ERR_F("mmc-check error code: %d", ret);
                display_system_note("mmc-check failed");
                return;
        } else if (ret < 0) {
                ULOG_ERR_F("exec_prog error code: %d", ret);
                display_system_note("exec_prog failed");
                return;
        }
#if 0
        l->corrupt = 0;
        set_mmc_corrupted_flag(FALSE, mmc);

        if (mount_volumes(mmc)) {
                display_system_note(_("card_memory_card_repaired"));
        } else {
                display_system_note(MSG_UNABLE_TO_REPAIR);
        }
#endif
}

static int mount_volumes(mmc_info_t *mmc, gboolean show_errors)
{
        const char *mount_args[] = {MMC_MOUNT_COMMAND, NULL, NULL, NULL};
        volume_list_t *l;
        const char *udi = NULL, *device = NULL;
        int ret, count = 0;
       
        l = get_nth_volume(mmc, mmc->preferred_volume);
        /*
        if (l && l->corrupt) {
                ULOG_DEBUG_F("partition %d is corrupt",
                             mmc->preferred_volume);
                return 0;
        }
        */

        if (l == NULL || l->udi == NULL) {
                ULOG_DEBUG_F("partition %d not found", mmc->preferred_volume);
                if (mmc->preferred_volume != 1 && mmc->internal_card) {
                        /* workaround for not yet partitioned systems */
                        ULOG_DEBUG_F("falling back to partition 1!");
                        l = get_nth_volume(mmc, 1);
                        if (l == NULL || l->udi == NULL)
                                return 0;
                } else
                        return 0;
        }
        udi = l->udi;
        device = l->dev_name;

        if (device == NULL) {
                ULOG_ERR_F("couldn't get device for %s", udi);
                return 0;
        }

        ULOG_DEBUG_F("trying to mount %s", device);

        mount_args[1] = device;
        mount_args[2] = mmc->mount_point;
        ret = exec_prog(MMC_MOUNT_COMMAND, mount_args);
        if (ret == 0) {
                l->mountpoint = strdup(mmc->mount_point);
                l->corrupt = 0;
                possibly_turn_swap_on(mmc);
                set_mmc_corrupted_flag(FALSE, mmc);
                count = 1;
        } else if (ret == 2) {
                ULOG_DEBUG_F("exec_prog returned %d", ret);
                l->corrupt = 1; /* 10s check failed */
                inform_mmc_swapping(FALSE, mmc);
                set_mmc_corrupted_flag(TRUE, mmc);
                if (show_errors)
                        open_closeable_dialog(OSSO_GN_NOTICE,
                                _("card_ia_corrupted"), "OMG!");
        } else {
                ULOG_DEBUG_F("exec_prog returned %d", ret);
                l->corrupt = 1;
                inform_mmc_swapping(FALSE, mmc);
                set_mmc_corrupted_flag(TRUE, mmc);
                if (show_errors) {
                        if (mmc->internal_card)
                                display_dialog(
                                        _("card_ib_unknown_format_device"));
                        else
                                display_dialog(
                                        _("card_ib_unknown_format_card"));
                }
        }
        inform_mmc_used_over_usb(FALSE, mmc);
        return count;
}

static int do_unmount(const char *mountpoint)
{
        const char* umount_args[] = {MMC_UMOUNT_COMMAND, NULL, NULL};
        int ret;

        emit_gnomevfs_pre_unmount(mountpoint);
        umount_args[1] = mountpoint;
        ret = exec_prog(MMC_UMOUNT_COMMAND, umount_args);
        if (ret == 0) {
                return 1;
        } else {
                return 0;
        }
}

static void discard_volume(mmc_info_t *mmc, const char *udi)
{
        volume_list_t *l;
        int found = 0;
       
        for (l = &mmc->volumes; l != NULL; l = l->next) {
                if (l->udi != NULL && strcmp(udi, l->udi) == 0) {
                        found = 1;
                        break;
                }
        }
        if (!found) {
                ULOG_DEBUG_F("%s not found from volume list", udi);
                return;
        }
        if (l->mountpoint == NULL) {
                /* this is a bug or OOM */
                ULOG_DEBUG_F("mount point not known for %s", udi);
                rm_volume_from_list(&mmc->volumes, udi);
                return;
        }
        if (!do_unmount(l->mountpoint)) {
                ULOG_INFO_F("couldn't unmount %s", udi);
        }
        rm_volume_from_list(&mmc->volumes, udi);
}

/* try to unmount all volumes on the list */
int unmount_volumes(mmc_info_t *mmc)
{
        int all_unmounted = 1;
       
        if (mmc->control_partitions) {
            volume_list_t *l;
            for (l = &mmc->volumes; l != NULL; l = l->next) {
                if (l->udi != NULL) {
                        int prop;
                        /* TODO: cache is_mounted info for speed */
                        prop = get_prop_bool(l->udi, "volume.is_mounted");
                        if (!prop) {
                                ULOG_DEBUG_F("%s is not mounted", l->udi);
                                /* FIXME: This is a workaround for
                                 * lower-level issues. Sometimes is_mounted
                                 * property cannot be trusted. */
                                if (l->mountpoint != NULL
                                    && do_unmount(l->mountpoint)) {
                                        /* unmount succeeded or it
                                         * was not mounted */
                                }
                                continue;
                        }
                        if (l->mountpoint == NULL) {
                                ULOG_DEBUG_F("mount point not known for %s",
                                             l->udi);
                                continue;
                        }
                        if (do_unmount(l->mountpoint)) {
                                ULOG_DEBUG_F("unmounted %s", l->udi);
                        } else {
                                ULOG_INFO_F("couldn't unmount %s", l->udi);
                                all_unmounted = 0;
                        }
                }
            }
        } else {
                /* we only control single FAT volume */
                volume_list_t *vol;
                vol = get_nth_volume(mmc, mmc->preferred_volume);
                if (vol == NULL) {
                        ULOG_ERR_F("could not find partition number %d",
                                   mmc->preferred_volume);
                        all_unmounted = 0;
                } else {
                        if (vol->mountpoint && do_unmount(vol->mountpoint)) {
                                ULOG_DEBUG_F("unmounted %s", vol->udi);
                        } else {
                                ULOG_INFO_F("couldn't unmount %s", vol->udi);
                                all_unmounted = 0;
                        }
                }
        }
        return all_unmounted;
}

static int event_in_cover_open(mmc_event_t e, mmc_info_t *mmc,
                               const char *arg)
{
        int ret = 1;
        switch (e) {
                case E_CLOSED:
                        ULOG_DEBUG_F("E_CLOSED for %s", mmc->name);
                        /* notify applications about closed cover */
                        inform_mmc_cover_open(FALSE, mmc);
                        if (!ignore_cable && in_mass_storage_mode()
                            && !device_locked) {
                                usb_share_card(mmc, TRUE);
                        } else {
                                init_mmc_volumes(mmc);
                                update_mmc_label(mmc);
                                if (mount_volumes(mmc, TRUE)) {
                                        /*
                                        display_dialog(
                                                MSG_MEMORY_CARD_AVAILABLE);
                                        if (desktop_started) {
                                                check_install_file(mmc);
                                        }
                                                */
                                }
                        }
                        mmc->state = S_COVER_CLOSED;
                        break;
                case E_RENAME:
                        ULOG_WARN_F("improper state");
                        break;
                case E_FORMAT:
                        ULOG_WARN_F("improper state");
                        break;
                case E_REPAIR:
                case E_CHECK:
                        ULOG_WARN_F("improper state");
                        break;
                case E_VOLUME_REMOVED:
                        ULOG_DEBUG_F("E_VOLUME_REMOVED for %s", mmc->name);
                        discard_volume(mmc, arg);
                        break;
                case E_VOLUME_ADDED:
                        ULOG_DEBUG_F("E_VOLUME_ADDED for %s", mmc->name);
                        if (mmc->skip_banner) mmc->skip_banner = FALSE;
                        break;
                case E_DEVICE_REMOVED:
                        ULOG_DEBUG_F("E_DEVICE_REMOVED for %s", mmc->name);
                        inform_device_present(FALSE, mmc);
                        break;
                case E_DEVICE_ADDED:
                        ULOG_DEBUG_F("E_DEVICE_ADDED for %s", mmc->name);
                        /* no action because the cover is open */
                        inform_device_present(TRUE, mmc);
                        break;
                case E_ENABLE_SWAP:
                        send_error("improper state");
                        break;
                case E_DISABLE_SWAP:
                        send_error("improper state");
                        break;
                case E_INIT_CARD:
                        ULOG_DEBUG_F("E_INIT_CARD for %s", mmc->name);
                        /* cover is open, the device should not be used */
                        inform_device_present(FALSE, mmc);
                        break;
                case E_PLUGGED:
                        ULOG_DEBUG_F("E_PLUGGED for %s", mmc->name);
                        break;
                case E_DETACHED:
                        ULOG_DEBUG_F("E_DETACHED for %s", mmc->name);
                        break;
                default:
                        ULOG_ERR_F("unsupported event %d for %s",
                                   e, mmc->name);
        }
        return ret;
}

static void handle_disable_swap(mmc_info_t *mmc)
{
        gboolean retval = TRUE;

        if (mmc->swap_off_with_close_apps) {
                /* ask TN to close applications */
                retval = send_exit_signal();
                if (retval) {
                        int max_wait = RAM_WAITING_TIMEOUT;
                        for (; max_wait > 0; --max_wait) {
                                /* wait for more RAM */
                                /* FIXME: should handle events in
                                 * the meanwhile */
                                sleep(1);
                                if (swap_can_switch_off()) {
                                        break;
                                }
                        }
                        if (max_wait == 0) {
                                ULOG_ERR_F("Not enough RAM to swapoff "
                                           "after %d s of waiting",
                                           RAM_WAITING_TIMEOUT);
                                send_error("not enough RAM");
                                return;
                        }
                }
        }
        if (retval) {
                retval = possibly_turn_swap_off_nocheck(mmc);
        }
        if (retval) {
                send_reply();
        } else {
                send_error("failure");
        }
}

void show_usb_sharing_failed_dialog(mmc_info_t *in, mmc_info_t *ex)
{
        char buf[MAX_MSG_LEN + 1];
        buf[0] = '\0';

        if (in && in->display_name[0] == '\0') {
                strncpy(in->display_name,
                        (const char*)dgettext("hildon-fm",
                        "sfil_li_memorycard_internal"), 99);
        }
        if (ex && ex->display_name[0] == '\0') {
                strncpy(ex->display_name,
                        (const char*)dgettext("hildon-fm",
                        "sfil_li_memorycard_removable"), 99);
        }

        if (in && ex) {
                snprintf(buf, MAX_MSG_LEN,
                         MSG_USB_MEMORY_CARDS_IN_USE,
                         ex->display_name);
        } else {
                if (ex) {
                        /*
                        mmc_info_t *mmc = in;
                        mmc = ex;
                        */
                        /* error string missing for this case */
                        return;
                } else
                        snprintf(buf, MAX_MSG_LEN,
                                 MSG_USB_MEMORY_CARD_IN_USE);
        }
        display_system_note(buf);
}

static void open_dialog_helper(mmc_info_t *mmc)
{
        /*
        if (mmc->dialog_id == -1 && mmc->swap_dialog_id == -1) {
                mmc->dialog_id = open_closeable_dialog(OSSO_GN_WARNING,
                                     MSG_UNMOUNT_MEMORY_CARD_IN_USE, "");
        }
        */
}

static int event_in_cover_closed(mmc_event_t e, mmc_info_t *mmc,
                                 const char *arg)
{
        int ret = 1;
        switch (e) {
                case E_OPENED:
                        ULOG_DEBUG_F("E_OPENED for %s", mmc->name);
                        /* notify applications about opened cover */
                        inform_mmc_cover_open(TRUE, mmc);

                        if (!ignore_cable && in_mass_storage_mode()) {
                                unshare_usb_shared_card(mmc);
                                mmc->state = S_COVER_OPEN;
                                break;
                        }
                        possibly_turn_swap_off(NORMAL_DIALOG, mmc);
                        if (!unmount_volumes(mmc)) {
                                open_dialog_helper(mmc);
                                setup_s_unmount_pending(mmc);
                                mmc->state = S_UNMOUNT_PENDING;
                        } else {
                                mmc->state = S_COVER_OPEN;
                        }
                        break;
                case E_PLUGGED:
                        if (!ignore_cable && in_mass_storage_mode()
                            && !device_locked) {
                                possibly_turn_swap_off(NO_DIALOG, mmc);
                                if (!unmount_volumes(mmc)) {
                                        ret = 0;
                                } else {
                                        usb_share_card(mmc, TRUE);
                                }
                        }
                        break;
                case E_DETACHED:
                        if (!ignore_cable && in_mass_storage_mode()) {
                                unshare_usb_shared_card(mmc);
                                init_mmc_volumes(mmc);
                                update_mmc_label(mmc);
                                mount_volumes(mmc, TRUE);
                        }
                        break;
                case E_RENAME:
                        handle_e_rename(mmc);
                        break;
                case E_FORMAT:
                        handle_e_format(mmc);
                        break;
                case E_REPAIR:
                        handle_e_repair(mmc);
                        break;
                case E_CHECK:
                        handle_e_check(mmc);
                        break;
                case E_VOLUME_ADDED:
                        ULOG_DEBUG_F("E_VOLUME_ADDED for %s", mmc->name);
                        if (ignore_cable || !in_mass_storage_mode()) {
                                update_mmc_label(mmc);
                                if (mount_volumes(mmc, TRUE)) {
                                        /*
                                        if (!mmc->skip_banner) {
                                                display_dialog(
                                                 MSG_MEMORY_CARD_AVAILABLE);
                                        }
                                        if (desktop_started) {
                                                check_install_file(mmc);
                                        }
                                        */
                                }
                        }
                        if (mmc->skip_banner) mmc->skip_banner = FALSE;
                        break;
                case E_VOLUME_REMOVED:
                        ULOG_DEBUG_F("E_VOLUME_REMOVED for %s", mmc->name);
                        if (ignore_cable || !in_mass_storage_mode()) {
                                discard_volume(mmc, arg);
                        }
                        break;
                case E_DEVICE_ADDED:
                        ULOG_DEBUG_F("E_DEVICE_ADDED for %s", mmc->name);
                        inform_device_present(TRUE, mmc);
                        if (!ignore_cable && in_mass_storage_mode()
                            && !device_locked) {
                                usb_share_card(mmc, TRUE);
                        }
                        break;
                case E_DEVICE_REMOVED:
                        ULOG_DEBUG_F("E_DEVICE_REMOVED for %s", mmc->name);
                        inform_device_present(FALSE, mmc);
                        if (!ignore_cable && in_mass_storage_mode()) {
                                unshare_usb_shared_card(mmc);
                        } else {
                                unmount_volumes(mmc);
                        }
                        break;
                case E_ENABLE_SWAP:
                        if (possibly_turn_swap_on(mmc)) {
                                send_reply();
                        } else {
                                send_error("failure");
                        }
                        break;
                case E_DISABLE_SWAP:
                        handle_disable_swap(mmc);
                        break;
                case E_INIT_CARD:
                        ULOG_DEBUG_F("E_INIT_CARD for %s", mmc->name);
                        if (mmc->whole_device != NULL) {
                                inform_device_present(TRUE, mmc);
                                if (!ignore_cable && in_mass_storage_mode()
                                    && !device_locked) {
                                        usb_share_card(mmc, FALSE);
                                } else if (!in_peripheral_wait_mode()) {
                                        update_mmc_label(mmc);
                                        mount_volumes(mmc, TRUE);
                                }
                        } else
                                ULOG_DEBUG_F("%s whole_device missing",
                                             mmc->name);
                        break;
                default:
                        ULOG_ERR_F("unsupported event %d for %s",
                                   e, mmc->name);
        }
        return ret;
}

static int event_in_unmount_pending(mmc_event_t e, mmc_info_t *mmc,
                                    const char *arg)
{
        int ret = 1;
        switch (e) {
                case E_CLOSED:
                        ULOG_DEBUG_F("E_CLOSED for %s", mmc->name);
                        dismantle_s_unmount_pending(mmc);
                        inform_mmc_cover_open(FALSE, mmc);
                        CLOSE_DIALOG
                        CLOSE_SWAP_DIALOG
                        if (!ignore_cable && in_mass_storage_mode()
                            && !device_locked) {
                                usb_share_card(mmc, TRUE);
                        } else {
                                init_mmc_volumes(mmc); /* re-read volumes */
                                mount_volumes(mmc, TRUE);
                        }
                        mmc->state = S_COVER_CLOSED;
                        break;
                case E_RENAME:
                        ULOG_WARN_F("improper state");
                        break;
                case E_FORMAT:
                        ULOG_WARN_F("improper state");
                        break;
                case E_REPAIR:
                case E_CHECK:
                        ULOG_WARN_F("improper state");
                        break;
                case E_UNMOUNT_TIMEOUT:
                        ULOG_DEBUG_F("E_UNMOUNT_TIMEOUT for %s", mmc->name);
                        mmc->unmount_pending_timer_id = 0;
                        if (!unmount_volumes(mmc)) {
                                open_dialog_helper(mmc);
                                setup_s_unmount_pending(mmc);
                        } else {
                                CLOSE_DIALOG
                                CLOSE_SWAP_DIALOG
                                mmc->state = S_COVER_OPEN;
                        }
                        break;
                case E_VOLUME_REMOVED:
                        ULOG_DEBUG_F("E_VOLUME_REMOVED for %s", mmc->name);
                        discard_volume(mmc, arg);
                        if (unmount_volumes(mmc)) {
                                dismantle_s_unmount_pending(mmc);
                                CLOSE_DIALOG
                                CLOSE_SWAP_DIALOG
                                mmc->state = S_COVER_OPEN;
                        }
                        break;
                case E_ENABLE_SWAP:
                        send_error("improper state");
                        break;
                case E_DISABLE_SWAP:
                        send_error("improper state");
                        break;
                default:
                        ULOG_ERR_F("unsupported event %d for %s",
                                   e, mmc->name);
        }
        return ret;
}

int handle_event(mmc_event_t e, mmc_info_t *mmc, const char *arg)
{
        int ret = 1;
        if (mmc->whole_device == NULL) {
                ULOG_DEBUG_F("whole_device unknown for %s", mmc->name);
                return ret;
        }
        switch (mmc->state) {
                case S_COVER_OPEN:
                        ret = event_in_cover_open(e, mmc, arg);
                        break;
                case S_COVER_CLOSED:
                        ret = event_in_cover_closed(e, mmc, arg);
                        break;
                case S_UNMOUNT_PENDING:
                        ret = event_in_unmount_pending(e, mmc, arg);
                        break;
                default:
                        ULOG_ERR_F("unknown state %d", mmc->state);
        }
        return ret;
}

