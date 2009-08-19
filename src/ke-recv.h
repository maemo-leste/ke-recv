/**
  @file ke-recv.h
  
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

#ifndef KE_RECV_H_
#define KE_RECV_H_

#include <stdlib.h>
#include <sys/wait.h>
#include <osso-log.h>
#include <glib-2.0/glib.h>
#include <libosso.h>
#include <assert.h>

#ifndef __USE_BSD
#define __USE_BSD
#define KE_RECV_DEFINED_USE_BSD
#endif
#include <unistd.h>
#ifdef KE_RECV_DEFINED_USE_BSD
#undef __USE_BSD
#endif

#include <string.h>
#include <errno.h>
#include <locale.h>
#include <libintl.h>
#include <config.h>

#ifndef DBUS_API_SUBJECT_TO_CHANGE
#define DBUS_API_SUBJECT_TO_CHANGE 1
#endif
#include <dbus/dbus.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _(X) gettext(X)

#define APPL_NAME "ke_recv"
#define APPL_VERSION "1"

#define SVC_NAME "org.kernel.kevent"
#define OP_NAME "/org/kernel/kevent"

#define RENAME_OP "/com/nokia/ke_recv/rename"
#define FORMAT_OP "/com/nokia/ke_recv/format"
#define INTERNAL_RENAME_OP "/com/nokia/ke_recv/internal_rename"
#define INTERNAL_FORMAT_OP "/com/nokia/ke_recv/internal_format"
#define VOLUME_LABEL_FILE "/tmp/.mmc-volume-label"
#define INTERNAL_VOLUME_LABEL_FILE "/tmp/.internal-mmc-volume-label"

#define IF_NAME SVC_NAME
#define CHANGE_SIG "change"
#define ADD_SIG "add"
#define REMOVE_SIG "remove"
#define COVER_OPEN_STR "open"  /* MMC cover open */
#define COVER_CLOSED_STR "closed"
#define DETACHED_STR "disconnected" /* USB cable detached */

#define CAMERA_OUT_STR "active"
#define CAMERA_TURNED_STR "inactive"

#define KEVENT_MATCH_RULE "type='signal',interface='" IF_NAME "'"

/* MCE interface */
#define MCE_SERVICE "com.nokia.mce"
#define MCE_REQUEST_IF "com.nokia.mce.request"
#define MCE_REQUEST_OP "/com/nokia/mce/request"
#define MCE_SIGNAL_IF "com.nokia.mce.signal"
#define MCE_SIGNAL_OP "/com/nokia/mce/signal"
#define MCE_GET_DEVICELOCK_MSG "get_devicelock_mode"
#define MCE_DEVICELOCK_SIG "devicelock_mode_ind"
#define MCE_SHUTDOWN_SIG "shutdown_ind"
#define MCE_LOCKED_STR "locked"
#define MCE_MATCH_RULE "type='signal',interface='" MCE_SIGNAL_IF "'"

/* low-memory signal from kdbusd */
#define LOWMEM_SIGNAL_OP "/org/kernel/kernel/high_watermark"
#define SYSFS_LOWMEM_STATE_FILE "/sys/kernel/high_watermark"

/* background killing signal */
#define BGKILL_SIGNAL_OP "/org/kernel/kernel/low_watermark"
#define SYSFS_BGKILL_STATE_FILE "/sys/kernel/low_watermark"

#define TN_BGKILL_ON_SIGNAL_NAME "bgkill_on"
#define TN_BGKILL_ON_SIGNAL_IF "com.nokia.ke_recv.bgkill_on"
#define TN_BGKILL_ON_SIGNAL_OP "/com/nokia/ke_recv/bgkill_on"
#define TN_BGKILL_OFF_SIGNAL_NAME "bgkill_off"
#define TN_BGKILL_OFF_SIGNAL_IF "com.nokia.ke_recv.bgkill_off"
#define TN_BGKILL_OFF_SIGNAL_OP "/com/nokia/ke_recv/bgkill_off"

/* lowmem signals */
#define LOWMEM_ON_SIGNAL_NAME "lowmem_on"
#define LOWMEM_ON_SIGNAL_IF "com.nokia.ke_recv.lowmem_on"
#define LOWMEM_ON_SIGNAL_OP "/com/nokia/ke_recv/lowmem_on"
#define LOWMEM_OFF_SIGNAL_NAME "lowmem_off"
#define LOWMEM_OFF_SIGNAL_IF "com.nokia.ke_recv.lowmem_off"
#define LOWMEM_OFF_SIGNAL_OP "/com/nokia/ke_recv/lowmem_off"

/* user lowmem signal */
#define USER_LOWMEM_OFF_SIGNAL_OP "/com/nokia/ke_recv/user_lowmem_off"
#define USER_LOWMEM_OFF_SIGNAL_IF "com.nokia.ke_recv.user_lowmem_off"
#define USER_LOWMEM_OFF_SIGNAL_NAME "user_lowmem_off"
#define USER_LOWMEM_ON_SIGNAL_OP "/com/nokia/ke_recv/user_lowmem_on"
#define USER_LOWMEM_ON_SIGNAL_IF "com.nokia.ke_recv.user_lowmem_on"
#define USER_LOWMEM_ON_SIGNAL_NAME "user_lowmem_on"

/* statusbar defines */
#define STATUSBAR_SERVICE "com.nokia.statusbar"
#define STATUSBAR_IF "com.nokia.statusbar"
#define STATUSBAR_OP "/com/nokia/statusbar"

/* MMC swapping interface */
#define MMC_SWAP_ON_IF "com.nokia.ke_recv.mmc_swap_on"
#define MMC_SWAP_ON_OP "/com/nokia/ke_recv/mmc_swap_on"
#define INTERNAL_MMC_SWAP_ON_OP "/com/nokia/ke_recv/internal_mmc_swap_on"
#define MMC_SWAP_ON_NAME "mmc_swap_on"
#define MMC_SWAP_OFF_IF "com.nokia.ke_recv.mmc_swap_off"
#define MMC_SWAP_OFF_OP "/com/nokia/ke_recv/mmc_swap_off"
#define INTERNAL_MMC_SWAP_OFF_OP "/com/nokia/ke_recv/internal_mmc_swap_off"
#define MMC_SWAP_OFF_NAME "mmc_swap_off"

/* Exit signal definitions */
#define AK_BROADCAST_IF "com.nokia.osso_app_killer"
#define AK_BROADCAST_OP "/com/nokia/osso_app_killer"
#define AK_BROADCAST_EXIT "exit"

/* PC suite, mass storage, charging request */
#define ENABLE_PCSUITE_OP "/com/nokia/ke_recv/enable_pcsuite"
#define ENABLE_MASS_STORAGE_OP "/com/nokia/ke_recv/enable_mass_storage"
#define ENABLE_CHARGING_OP "/com/nokia/ke_recv/enable_charging"

#define INVALID_DIALOG_RESPONSE -666

typedef enum {
        S_INVALID_USB_STATE = 0,
        S_CABLE_DETACHED,
        S_PERIPHERAL_WAIT,
        S_HOST,
        S_EJECTING,
        S_EJECTED,
        S_MASS_STORAGE,
        S_CHARGING,
        S_PCSUITE,
        S_CHARGER_PROBE
} usb_state_t;

typedef enum {
        E_CABLE_DETACHED,
        E_EJECT,
        E_EJECT_CANCELLED,
        E_ENTER_HOST_MODE,
        E_ENTER_PERIPHERAL_WAIT_MODE,
        /* the three next ones are for USB plugin's requests */
        E_ENTER_MASS_STORAGE_MODE,
        E_ENTER_CHARGING_MODE,
        E_ENTER_PCSUITE_MODE,
        E_ENTER_CHARGER_PROBE
} usb_event_t;

typedef enum {
        S_INVALID = 0,
        S_COVER_OPEN,
        S_COVER_CLOSED,
        S_UNMOUNT_PENDING
} mmc_state_t;

typedef enum {
	COVER_INVALID = 0,
	COVER_OPEN,
	COVER_CLOSED
} mmc_cover_t;

typedef enum {
	DEVICE_INVALID = 0,
	DEVICE_PRESENT,
	DEVICE_ABSENT
} mmc_device_t;

typedef struct volume_list_t_ {
        char *udi;
        char *mountpoint;
        char *dev_name;
        int volume_number;
        int corrupt;
        struct volume_list_t_ *next;
} volume_list_t;

typedef struct {
        gboolean internal_card;
        gboolean skip_banner;
        char name[10]; /* used in debug printouts */
        char display_name[100]; /* localised volume label */
        mmc_state_t state;
        mmc_cover_t cover_state;

        volume_list_t volumes;
        int preferred_volume;  /* volume (partition) to do operations on */
        int control_partitions;  /* whether or not we control the whole
                                    device, not just one partition */

        char desired_label[12];
        const char *mount_point;
        const char *swap_location;

        char *udi;
        char *storage_parent_udi;
        char *storage_udi;
        char *whole_device;
        const char *volume_label_file;

        /* GConf key names */
        const char *presence_key;
        const char *corrupted_key;
        const char *used_over_usb_key;
        const char *cover_open_key;
        const char *swapping_key;

        /* DBus object paths */
        const char *rename_op;
        const char *format_op;
        const char *swap_on_op;
        const char *swap_off_op;

        const char *cover_udi;

        guint mount_timer_id;
        guint unmount_pending_timer_id;
        gboolean swap_off_with_close_apps;

        dbus_int32_t dialog_id;
        dbus_int32_t swap_dialog_id;
        char *swap_dialog_response;
} mmc_info_t;

typedef struct storage_info_t_ {
        char name[10]; /* used in debug printouts */
        volume_list_t volumes;
        char *storage_udi;
        char *whole_device;
        struct storage_info_t_ *next;
} storage_info_t;

typedef struct mmc_list_t_ {
        mmc_info_t *mmc;
        struct mmc_list_t_ *next;
} mmc_list_t;

typedef enum {
	CABLE_INVALID = 0,
	CABLE_DETACHED,
	CABLE_ATTACHED
} mmc_cable_t;

typedef enum {
	CAMERA_INVALID = 0,
	CAMERA_OUT,
	CAMERA_IN
} camera_out_state_t;

typedef enum {
	CAMERA_TURNED_INVALID = 0,
	CAMERA_TURNED_OUT,
	CAMERA_TURNED_IN
} camera_turned_state_t;

/* public functions */
void send_error(const char* n);
void send_reply(void);
gboolean get_device_lock(void);
dbus_uint32_t open_closeable_dialog(osso_system_note_type_t type,
                                    const char *msg, const char *btext);
void close_closeable_dialog(dbus_uint32_t id);
void show_infobanner(const char *msg);
gint get_dialog_response(dbus_int32_t id);
/*
gboolean send_exit_signal(void);
*/
void send_systembus_signal(const char *op, const char *iface,
                                           const char *name);
char* find_by_cap_and_prop(const char *capability,
                           const char *property, const char *value);
char *get_prop_string(const char *udi, const char *property);
int get_prop_bool(const char *udi, const char *property);
int get_prop_int(const char *udi, const char *property);
void rm_volume_from_list(volume_list_t *l, const char *udi);
int init_mmc_volumes(mmc_info_t *mmc);
void clear_volume_list(volume_list_t *l);
int in_mass_storage_mode(void);
int in_peripheral_wait_mode(void);
usb_state_t get_usb_state(void);
/*
int check_install_file(const mmc_info_t *mmc);
*/

#ifdef __cplusplus
}
#endif
#endif /* KE_RECV_H_ */
