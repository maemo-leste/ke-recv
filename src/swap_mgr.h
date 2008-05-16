/* ========================================================================= *
 * Copyright (C) 2004-2006 Nokia Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * File: swap_mgr.h
 *
 * Contact: Leonid Moiseichuk <leonid.moiseichuk@nokia.com>
 *
 * Description:
 *    Swap file management.
 *
 * History:
 *
 * 13-Mar-2006 Leonid Moiseichuk
 * - added callback for swap file creation for UI.
 *
 * 28-Feb-2006 Leonid Moiseichuk
 * - added swap_enabled function to check is swap enabled or not.
 * - file renamed to swap_mgr according to letter.
 *
 * 27-Feb-2006 Leonid Moiseichuk
 * - added 3 service functions for minimal and maximal size.
 *
 * 24-Feb-2006 Leonid Moiseichuk
 * - initial version created.
 * ========================================================================= */

#ifndef SWAP_MGR_H_USED
#define SWAP_MGR_H_USED

/* ========================================================================= *
 * Includes
 * ========================================================================= */

/* ========================================================================= *
 * Definitions.
 * ========================================================================= */

/* Environment variable with swap file mount location */
#define SWAP_VAR           "OSSO_SWAP"

/* Swap file name */
#define SWAP_NAME          ".swap"

/* Minimally available size of swap file [bytes] */
#define SWAP_MINIMUM        (8 << 20)

/* Granularity of swap file [bytes] */
#define SWAP_GRANULARITY    (8 << 20)

/* Callback for swap file creation. It may take quite a long time, so */
/* this callback may be used to update screen according to progress.  */
/* The following interface is supported:                              */
/* -> current_page - the number of the current page to be written into*/
/*   the swap file from 0 to latest_page inclusively.                 */
/* -> latest_page - the number of the latest page to be written.      */
/* <- return code shall be handled in swap_create this way:           */
/*    0  - lets continue swap file creation;                          */
/*    !0 - interrupt swap file creation and return this code higher   */
typedef int (*SWAP_CREATE_CALLBACK)(unsigned current_page, unsigned latest_page);

/* ========================================================================= *
 * Methods.
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * swap_permitted -- Detects is swap permitted to be created and activated.
 * parameters: nothing.
 * returns: 1 if permitted and 0 if not.
 * ------------------------------------------------------------------------- */
unsigned swap_permitted(void);

/* ------------------------------------------------------------------------- *
 * swap_available -- Returns amount of memory available in swap file.
 * parameters: nothing.
 * returns:
 *    0     - swap file is not created or permitted
 *    value - amount of available swap memory in bytes.
 * ------------------------------------------------------------------------- */
unsigned swap_available(void);

/* ------------------------------------------------------------------------- *
 * swap_validate_size -- Validates proposed size of swap file.
 * parameters: proposed size in bytes.
 * returns: validated size in bytes or 0 in case of error.
 * ------------------------------------------------------------------------- */
unsigned swap_validate_size(unsigned size);

/* ------------------------------------------------------------------------- *
 * swap_minimal_size -- Detects available space on file system at
 *       SWAP_MGR_VAR and generate minimal size of swap file.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - validated minimal swap size in bytes (SWAP_MGR_MINIMUM).
 * ------------------------------------------------------------------------- */
unsigned swap_minimal_size(void);

/* ------------------------------------------------------------------------- *
 * swap_automatic_size -- Detects available size on file system at
 *       SWAP_MGR_VAR and generate possible size of swap file.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - validated recommended swap size in bytes if everything is Ok.
 * ------------------------------------------------------------------------- */
unsigned swap_automatic_size(void);

/* ------------------------------------------------------------------------- *
 * swap_maximal_size -- Detects available space on file system at
 *       SWAP_MGR_VAR and generate maximal size of swap file.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - validated maximal swap size in bytes.
 * ------------------------------------------------------------------------- */
unsigned swap_maximal_size(void);

/* ------------------------------------------------------------------------- *
 * swap_size_granularity -- Returns swap size granularity.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - granularity of swap file size.
 * ------------------------------------------------------------------------- */
unsigned swap_size_granularity(void);

/* ------------------------------------------------------------------------- *
 * swap_create -- Create and format swap file.
 * parameters: size of swap file in bytes and callback function (may be NULL).
 * returns: 0 on success or errno (callback return code) in case of error.
 * ------------------------------------------------------------------------- */
int swap_create(unsigned size, SWAP_CREATE_CALLBACK callback);

/* ------------------------------------------------------------------------- *
 * swap_delete -- Delete (unmounted) swap file.
 * parameters: nothing.
 * returns: 0 on success or errno.
 * ------------------------------------------------------------------------- */
int swap_delete(void);

/* ------------------------------------------------------------------------- *
 * swap_enabled -- Checks, enabled swapping currently or not.
 * parameters: nothing.
 * returns: 0 - swap is not enabled, 1 - swap is enabled.
 * ------------------------------------------------------------------------- */
unsigned swap_enabled(void);

/* ------------------------------------------------------------------------- *
 * swap_can_switch_off -- Calculate can we safely switch swap off or not.
 * parameters: nothing.
 * returns: 0 we have to close applications, 1 - we can stop swap safely.
 * ------------------------------------------------------------------------- */
unsigned swap_can_switch_off(void);

/* ------------------------------------------------------------------------- *
 * swap_switch_on -- Attempt to swapon swap file.
 * parameters: nothing.
 * returns: 0 on success or errno.
 * ------------------------------------------------------------------------- */
int swap_switch_on(void);

/* ------------------------------------------------------------------------- *
 * swap_switch_off -- Attempt to swapoff swap file.
 * parameters: nothing.
 * returns: 0 on success or errno.
 * ------------------------------------------------------------------------- */
int swap_switch_off(void);


#endif /* SWAP_MGR_H_USED */


