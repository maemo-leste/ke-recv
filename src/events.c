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
#include <libhal.h>
#include <libvolume_id.h>
#include <fcntl.h>
 
extern DBusConnection *ses_conn;
extern gboolean desktop_started;

/* whether to handle USB cable or not */
static gboolean ignore_cable = FALSE;

/* whether or not the device is locked */
gboolean device_locked = FALSE;

typedef enum { USB_DIALOG, NORMAL_DIALOG, NO_DIALOG } swap_dialog_t;

GConfClient* gconfclient;

static int usb_share_card(mmc_info_t *mmc, gboolean show);
static int mount_volumes(mmc_info_t *mmc, const char *udi, gboolean show_errors);
static int do_unmount(const char *mountpoint, gboolean lazy);
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
                strcpy(mmc->display_name, "internal card");
        } else {
                strncpy(mmc->display_name,
                        (const char*)dgettext("hildon-fm",
                         "sfil_li_memorycard_removable"), 100);
        }
}

/* helper to hide backwards compatibility for the old layout */
static volume_list_t *get_internal_mmc_volume(mmc_info_t *mmc)
{
        volume_list_t *vol;
        vol = get_nth_volume(mmc, mmc->preferred_volume);
        if (vol && vol->dev_name) {
                return vol;
        } else {
                ULOG_ERR_F("could not find partition number %d",
                           mmc->preferred_volume);
                return NULL;
        }
}

static int volume_get_num(mmc_info_t *mmc, const char *udi)
{
        char *dev = udi ? get_prop_string(udi, "block.device") : NULL;
        size_t len = dev ? strlen(dev) : 0;
        if (len && isdigit(dev[len-1]) && !mmc->internal_card)
                return dev[len-1]-'0';
        else
                return mmc->preferred_volume;
}

void update_mmc_label(mmc_info_t *mmc, const char *udi)
{
        int fd;
        char *part_device = NULL;
        volume_list_t *vol;
        struct volume_id *vid;
        const char *label, *cur;

        if (mmc->internal_card)
                vol = get_internal_mmc_volume(mmc);
        else
                vol = get_nth_volume(mmc, volume_get_num(mmc, udi));

        if (vol == NULL || vol->dev_name == NULL) {
                ULOG_ERR_F("could not find partition");
                return;
        }

        part_device = vol->dev_name;

        if (part_device == NULL) {
                ULOG_ERR_F("device name for partition number %d not found",
                           vol->volume_number);
error1:         set_localised_label(mmc);
                goto store_label;        
        }

        fd = open (part_device, O_RDONLY);
        if (fd < 0)
                goto error1;
        vid = volume_id_open_fd (fd);
        if (!vid)
        {
                ULOG_ERR_F("could not open partition %s", part_device);
error2:         close (fd);
                goto error1;
        }

        if (volume_id_probe_all (vid, 0, 0))
        {
                ULOG_ERR_F("failed to probe partition %s", part_device);
error3:         volume_id_close (vid);
                goto error2;
        }

        if (!volume_id_get_label (vid, &label) || !label)
        {
                ULOG_ERR_F("failed to retrieve volume label on partition %s", part_device);
                goto error3;
        }
        /* Check if volume label is not empty */
        cur = label;
        while (*cur == ' ' || *cur == '\t')
                cur++;

        if (!*cur)
                goto error3;

        strcpy (mmc->display_name, label);

        if (label[0] == '\0') {
                /* empty label */
                vol->desired_label[0] = '\0';
        } else {
                strncpy(vol->desired_label, label, 32);
                vol->desired_label[31] = '\0';
        }

        volume_id_close (vid);
        close (fd);

store_label:
        /* Store the volume label to file for GVFS2 */
        g_file_set_contents (mmc->volume_label_file, mmc->display_name,
                             strlen (mmc->display_name), NULL);
        /* If we failed... well, we failed. */
        /* GVFS will display 'mmc-undefined-name' then */
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
#if 0
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
                        display_system_note("swap file corrupt");
                }
                return FALSE;
        }
        inform_mmc_swapping(TRUE, mmc);
        return TRUE;
#endif
        return FALSE;
}

#if 0
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
#endif

static void possibly_turn_swap_off(swap_dialog_t dialog, mmc_info_t *mmc)
{
#if 0
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
                                        "in usb use",
                                        "close apps");
                } else {
                        mmc->swap_dialog_id =
                                open_closeable_dialog(OSSO_GN_WARNING,
                                        "card in use",
                                        "close apps");
                }
        }
#endif
}

void possibly_turn_swap_off_simple(mmc_info_t *mmc)
{
#if 0
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
#endif
}

static gboolean possibly_turn_swap_off_nocheck(mmc_info_t *mmc)
{
#if 0
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
#endif
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

        if (mmc->internal_card) {
                volume_list_t *vol;
                if ((vol = get_internal_mmc_volume(mmc)) == NULL)
                        return;
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

static int usb_share_card(mmc_info_t *mmc, gboolean show)
{
        const char *args[] = {NULL, NULL};
        char *dev;

        if (mmc->internal_card) {
                volume_list_t *vol;
                if ((vol = get_internal_mmc_volume(mmc)) == NULL) {
                        ULOG_ERR_F("volume not found for %s", mmc->name);
                        return 0;
                }
                dev = vol->dev_name;
        } else
                dev = mmc->whole_device;

        if (dev == NULL) {
                ULOG_DEBUG_F("dev unknown for %s", mmc->name);
                return 0;
        }

        if (mmc->storage_udi == NULL) {
                ULOG_DEBUG_F("device is not ready yet");
                return 0;
        }

        args[0] = dev;
        if (load_usb_driver(args)) {
                ULOG_INFO_F("USB mass storage module loaded for %s", dev);
                /*
                if (show) {
                        display_dialog("connected via usb");
                }
                */
                inform_mmc_used_over_usb(TRUE, mmc);
                return 1;
        } else {
                ULOG_ERR_F("failed to load USB mass storage module for %s",
                           dev);
                return 0;
        }
}

static void handle_e_rename(mmc_info_t *mmc, const char *udi)
{
        const char* args[] = {MMC_RENAME_PROG, NULL, NULL, NULL, NULL};
        int ret;
        volume_list_t *vol;

        if (mmc->internal_card)
                vol = get_internal_mmc_volume(mmc);
        else
                vol = get_nth_volume(mmc, volume_get_num(mmc, udi));

        if (vol == NULL || vol->dev_name == NULL) {
                ULOG_ERR_F("could not find partition");
                return;
        }

        ULOG_DEBUG_F("using device file %s", vol->dev_name);
        args[1] = vol->dev_name;
        args[2] = vol->desired_label;
        args[3] = vol->fstype;

        /* check validity of fat volume label */
        if (strcmp(args[3], "vfat") == 0) {
                ret = valid_fat_name(args[2]);
                assert(ret < 1 && ret > -3);
                if (ret == -1) {
                        ULOG_ERR_F("too long name");
                        return;
                } else if (ret == -2) {
                        ULOG_ERR_F("invalid characters");
                        return;
                }
        }

        ret = exec_prog(args[0], args);
        if (ret != 0) {
                ULOG_ERR_F("renaming failed: exec_prog returned %d",
                           ret);
                if (mmc->internal_card)
                        open_closeable_dialog(OSSO_GN_NOTICE,
                            MSG_MEMORY_CARD_IS_CORRUPTED_INT, "OMG!");
                else
                        open_closeable_dialog(OSSO_GN_NOTICE,
                            MSG_MEMORY_CARD_IS_CORRUPTED, "OMG!");
        }

        update_mmc_label(mmc, udi);
        hal_device_reprobe(udi);
        ULOG_DEBUG_F("successful renaming");
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

static void handle_e_format(mmc_info_t *mmc, const char *udi)
{
        int ret;
        const char* args[] = {MMC_FORMAT_PROG, NULL, NULL, NULL, NULL};
        volume_list_t *vol;
        char buf[100];

        if (mmc->internal_card)
                vol = get_internal_mmc_volume(mmc);
        else
                vol = get_nth_volume(mmc, volume_get_num(mmc, udi));

        ULOG_DEBUG_F("label for %s is '%s'", vol->dev_name, vol->desired_label);

        if (!mmc->control_partitions &&
            (vol == NULL || vol->dev_name == NULL)) {
                ULOG_ERR_F("could not find partition");
                return;
        }

        if (!mmc->control_partitions) {
                args[1] = vol->dev_name;
                args[2] = vol->desired_label;
                ULOG_DEBUG_F("using device file %s", args[1]);

                if (vol->mountpoint != NULL)
                        ret = do_unmount(vol->mountpoint, FALSE);
                else {
                        ULOG_DEBUG_F("no mountpoint, using dev_name");
                        ret = do_unmount(vol->dev_name, FALSE);
                }
        } else {
                args[1] = mmc->whole_device;
                if (vol == NULL || vol->dev_name == NULL) {
                        snprintf(buf, 100, "%sp%d", mmc->whole_device,
                                 volume_get_num(mmc, udi));
                } else
                        snprintf(buf, 100, "%s", vol->dev_name);
                args[2] = buf;
                args[3] = vol->desired_label;

                ret = unmount_volumes(mmc, FALSE);
        }
        if (!ret) {
                ULOG_INFO_F("memory card %s is in use", mmc->name);
                display_system_note(dgettext("osso-filemanager",
                                    "sfil_ni_mmc_format_mmc_in_use"));
                if (mmc->control_partitions)
                        /* we could have unmounted some other volumes */
                        mount_volumes(mmc, udi, TRUE);
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
                        mount_volumes(mmc, udi, TRUE);
                } else {
                        mmc->skip_banner = TRUE;
                }
                display_dialog(MSG_FORMATTING_COMPLETE);
        }
}


static void handle_e_repair(mmc_info_t *mmc, const char *arg)
{
        int ret, mounted;
        char *part_device = NULL, *udi = NULL;
        volume_list_t *l;
        const char* args[] = {"/usr/sbin/mmc-check", NULL, NULL};

        l = get_nth_volume(mmc, volume_get_num(mmc, arg));

        if (l && l->dev_name && l->udi) {
                part_device = l->dev_name;
                udi = l->udi;
        } else {
                ULOG_ERR_F("device name for the partition not found");
                display_system_note("unable to repair");
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
                display_system_note("unable to repair");
                return;
        } else if (ret > 0) {
                ULOG_ERR_F("mmc-check error code: %d", ret);
                display_system_note("unable to repair");
                return;
        } else if (ret < 0) {
                ULOG_ERR_F("exec_prog error code: %d", ret);
                display_system_note("unable to repair");
                return;
        }
        l->corrupt = 0;
        set_mmc_corrupted_flag(FALSE, mmc);

        init_mmc_volumes(mmc); /* re-init volumes */
        if (mount_volumes(mmc, udi, FALSE)) {
                display_system_note("memory card repaired");
        } else {
                display_system_note("unable to repair");
        }
}

static void handle_e_check(mmc_info_t *mmc, const char *arg)
{
        int ret, mounted;
        char *part_device = NULL, *udi = NULL;
        volume_list_t *l;
        const char* args[] = {"/usr/sbin/mmc-check", NULL, NULL};

        l = get_nth_volume(mmc, volume_get_num(mmc, arg));

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
                display_system_note("memory card repaired");
        } else {
                display_system_note("unable to repair");
        }
#endif
}

static int is_mounted(const char *path)
{
        struct stat st;
        struct stat st2;
        char buf[256];

        if (lstat(path, &st) != 0)
                return 0;
        if (!S_ISDIR(st.st_mode))
                return 1;

        memset(buf, 0, sizeof(buf));
        strncpy(buf, path, sizeof(buf) - 4);
        strcat(buf, "/..");

        if (stat(buf, &st2) != 0)
                return 1;

        if ((st.st_dev != st2.st_dev) ||
                (st.st_dev == st2.st_dev && st.st_ino == st2.st_ino))
                return 1;
        else
                return 0;
}

static int mount_volumes(mmc_info_t *mmc, const char *arg, gboolean show_errors)
{
        const char *mount_args[] = {MMC_MOUNT_COMMAND, NULL, NULL, NULL, NULL};
        volume_list_t *l;
        const char *udi = NULL, *device = NULL;
        char *mount_point = NULL;
        int ret, count = 0;
        int parts = 0;
       
        l = get_nth_volume(mmc, volume_get_num(mmc, arg));

        if (l == NULL || l->udi == NULL || l->dev_name == NULL) {
                ULOG_DEBUG_F("partition %d not found", volume_get_num(mmc, udi));
                return 0;
        }

        udi = l->udi;
        device = l->dev_name;

        if (!mmc->internal_card) {
                volume_list_t *l1;
                for (l1 = &mmc->volumes; l1 != NULL; l1 = l1->next)
                        parts++;
                if (parts > 1 && (strcmp(l->fstype, "vfat") || is_mounted(mmc->mount_point)))
                        mount_point = g_strdup_printf("%sp%d", mmc->mount_point, volume_get_num(mmc, udi));
        }

        if (!mount_point)
                mount_point = g_strdup(mmc->mount_point);

        ULOG_DEBUG_F("trying mount %s to %s", device, mount_point);

        mount_args[1] = device;
        mount_args[2] = mount_point;
        mount_args[3] = l->fstype;
        ret = exec_prog(MMC_MOUNT_COMMAND, mount_args);
        if (ret == 0) {
                l->mountpoint = mount_point;
                l->corrupt = 0;
                possibly_turn_swap_on(mmc);
                set_mmc_corrupted_flag(FALSE, mmc);
                count = 1;
        } else if (ret == 2) {
                /* is was mounted read-only */
                ULOG_DEBUG_F("exec_prog returned %d", ret);
                l->mountpoint = mount_point;
                l->corrupt = 1;
                inform_mmc_swapping(FALSE, mmc);
                set_mmc_corrupted_flag(TRUE, mmc);
        } else {
                /* corrupt beyond mounting, or unsupported format */
                ULOG_DEBUG_F("exec_prog returned %d", ret);
                g_free(mount_point);
                l->mountpoint = NULL;
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

static int do_unmount(const char *mountpoint, gboolean lazy)
{
        const char* umount_args[] = {MMC_UMOUNT_COMMAND, NULL, NULL, NULL};
        int ret;

        emit_gnomevfs_pre_unmount(mountpoint);
        umount_args[1] = mountpoint;

        if (lazy) umount_args[2] = "lazy";

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
        if (!do_unmount(l->mountpoint, TRUE)) {
                ULOG_INFO_F("couldn't unmount %s", udi);
        }
        rm_volume_from_list(&mmc->volumes, udi);
}

/* try to unmount all volumes on the list */
int unmount_volumes(mmc_info_t *mmc, gboolean lazy)
{
        int all_unmounted = 1;
       
        if (!mmc->internal_card) {
            volume_list_t *l;
            for (l = &mmc->volumes; l != NULL; l = l->next) {
                if (l->udi != NULL) {
                        if (l->mountpoint == NULL) {
                                ULOG_DEBUG_F("mount point not known for %s",
                                             l->udi);
                                continue;
                        }
                        if (do_unmount(l->mountpoint, lazy)) {
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
                vol = get_internal_mmc_volume(mmc);
                if (vol == NULL) {
                        ULOG_ERR_F("couldn't find partition for internal card");
                        all_unmounted = 0;
                } else {
                        char *arg;
                        if (vol->mountpoint)
                                arg = vol->mountpoint;
                        else
                                /* use device name in case it is not mounted
                                 * or mount point could be unknown */
                                arg = vol->dev_name;

                        if (do_unmount(arg, lazy)) {
                                ULOG_DEBUG_F("unmounted %s (%s)", arg,
                                             vol->udi);
                        } else {
                                ULOG_INFO_F("couldn't unmount %s (%s)", arg,
                                            vol->udi);
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
                                ret = usb_share_card(mmc, TRUE);
                        } else {
#if 0 /* cannot mount here anymore after kernel changes in Fremantle */
                                init_mmc_volumes(mmc);
                                update_mmc_label(mmc);
                                if (mount_volumes(mmc, TRUE)) {
                                        /*
                                        display_dialog(
                                                "card available");
                                        if (desktop_started) {
                                                check_install_file(mmc);
                                        }
                                                */
                                }
#endif
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
                        inform_mmc_cover_open(TRUE, mmc);
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

void show_usb_sharing_failed_dialog(mmc_info_t *in, mmc_info_t *ex,
                                    gboolean ext_failed)
{
        char buf[MAX_MSG_LEN + 1];
        buf[0] = '\0';

        if (ex && ex->display_name[0] == '\0') {
                strncpy(ex->display_name,
                        (const char*)dgettext("hildon-fm",
                        "sfil_li_memorycard_removable"), 99);
        }

        if (in && ex) {
                snprintf(buf, MAX_MSG_LEN,
                         MSG_USB_MEMORY_CARDS_IN_USE,
                         ex->display_name);
                display_system_note(buf);
        } else if (!ex) {
                /* internal card in use, no external card present */
                snprintf(buf, MAX_MSG_LEN,
                         MSG_USB_INT_MEMORY_CARD_IN_USE_NO_EXT);
                display_dialog(buf);
        } else {
                if (ext_failed) {
                        /* external card in use */
                        snprintf(buf, MAX_MSG_LEN,
                                 _("card_connected_via_usb_card"),
                                 ex->display_name);
                        display_dialog(buf);
                } else {
                        /* internal card in use */
                        snprintf(buf, MAX_MSG_LEN,
                                 _("card_connected_via_usb_device"),
                                 ex->display_name);
                        display_dialog(buf);
                }
        }
}

static void open_dialog_helper(mmc_info_t *mmc)
{
        /*
        if (mmc->dialog_id == -1 && mmc->swap_dialog_id == -1) {
                mmc->dialog_id = open_closeable_dialog(OSSO_GN_WARNING,
                                     "card in use", "");
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
                        if (!unmount_volumes(mmc, TRUE)) {
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
                                if (!unmount_volumes(mmc, FALSE)) {
                                        ret = 0;
                                } else {
                                        ret = usb_share_card(mmc, TRUE);
                                }
                        }
                        break;
                case E_DETACHED:
                        if (!ignore_cable && in_mass_storage_mode()) {
                                unshare_usb_shared_card(mmc);
                                init_mmc_volumes(mmc);
                                update_mmc_label(mmc, arg);
                                mount_volumes(mmc, arg, TRUE);
                        }
                        break;
                case E_RENAME:
                        handle_e_rename(mmc, arg);
                        break;
                case E_FORMAT:
                        handle_e_format(mmc, arg);
                        break;
                case E_REPAIR:
                        handle_e_repair(mmc, arg);
                        break;
                case E_CHECK:
                        handle_e_check(mmc, arg);
                        break;
                case E_VOLUME_ADDED:
                        ULOG_DEBUG_F("E_VOLUME_ADDED for %s", mmc->name);
                        if (ignore_cable || !in_mass_storage_mode()) {
                                update_mmc_label(mmc, arg);
                                if (mount_volumes(mmc, arg, TRUE)) {
                                        /*
                                        if (!mmc->skip_banner) {
                                                display_dialog(
                                                 "card available");
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
                                ret = usb_share_card(mmc, TRUE);
                        }
                        break;
                case E_DEVICE_REMOVED:
                        ULOG_DEBUG_F("E_DEVICE_REMOVED for %s", mmc->name);
                        inform_device_present(FALSE, mmc);
                        if (!ignore_cable && in_mass_storage_mode()) {
                                unshare_usb_shared_card(mmc);
                        } else {
                                unmount_volumes(mmc, TRUE);
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
                        inform_mmc_cover_open(FALSE, mmc);
                        if (mmc->whole_device != NULL) {
                                inform_device_present(TRUE, mmc);
                                if (!ignore_cable && in_mass_storage_mode()
                                    && !device_locked) {
                                        ret = usb_share_card(mmc, FALSE);
                                } else if (!in_peripheral_wait_mode()) {
                                        update_mmc_label(mmc, arg);
                                        mount_volumes(mmc, arg, TRUE);
                                        if (!mmc->internal_card) {
                                                volume_list_t *l1;
                                                for (l1 = &mmc->volumes; l1 != NULL; l1 = l1->next) {
                                                        update_mmc_label(mmc, l1->udi);
                                                        mount_volumes(mmc, l1->udi, TRUE);
                                                }
                                        }
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
                                ret = usb_share_card(mmc, TRUE);
                        } else {
                                init_mmc_volumes(mmc); /* re-read volumes */
                                mount_volumes(mmc, arg, TRUE);
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
                        if (!unmount_volumes(mmc, FALSE)) {
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
                        if (unmount_volumes(mmc, TRUE)) {
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

