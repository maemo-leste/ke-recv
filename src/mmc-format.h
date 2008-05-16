/**
  @file mmc-format.h
  MMC formatting program.

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

#ifndef MMC_FORMAT_H_
#define MMC_FORMAT_H_

#include <osso-log.h>
#include "ke-recv.h"
#include <gtk/gtk.h>
#include <hildon/hildon-banner.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* for some reason signal.h doesn't define sighandler_t */
typedef void (*sighandler_t)(int);

#define MMC_FORMAT_PROG_NAME "mmc-format"
#define MMC_PARTITIONING_COMMAND "/usr/sbin/osso-prepare-partition.sh"
#define MMC_FORMAT_COMMAND "/sbin/mkdosfs"

#define MSG_FORMATTING_MEMORY_CARD _("card_nw_formatting_memory_card")

#ifdef __cplusplus
}
#endif
#endif /* MMC_FORMAT_H_ */
