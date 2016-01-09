#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2004-2009 Nokia Corporation. All rights reserved.
#
# Contact: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License 
# version 2 as published by the Free Software Foundation. 
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

MUSB="/sys/devices/platform/musb_hdrc"
MODE="$MUSB/mode"
LUN0="$MUSB/gadget/gadget-lun0/file"
LUN1="$MUSB/gadget/gadget-lun1/file"

if [ ! -e $MUSB ]; then
  MUSB="/sys/bus/platform/devices/musb-hdrc.0.auto"
  MODE="$MUSB/mode"
  LUN0="$MUSB/gadget/lun0/file"
  LUN1="$MUSB/gadget/lun1/file"
fi

RC=0

if [ ! -e $LUN0 ] && /sbin/lsmod | grep -q g_nokia; then
    logger "$0: removing g_nokia"

    initctl emit G_NOKIA_REMOVE

    PNATD_PID=`pidof pnatd`
    if [ $? = 0 ]; then
        kill $PNATD_PID
    else
        logger "$0: pnatd is not running"
    fi
    OBEXD_PID=`pidof obexd`
    if [ $? = 0 ]; then
        kill -HUP $OBEXD_PID
    else
        logger "$0: obexd is not running"
    fi
    SYNCD_PID=`pidof syncd`
    if [ $? = 0 ]; then
        kill $SYNCD_PID
    else
        logger "$0: syncd is not running"
    fi

    /usr/sbin/pcsuite-disable.sh

    sleep 2
    /sbin/rmmod g_nokia
    if [ $? != 0 ]; then
        logger "$0: failed to rmmod g_nokia!"
        exit 1
    fi
fi

if [ ! -e $LUN0 ]; then
    /sbin/modprobe g_file_storage stall=0 luns=2 removable
    RC=$?
    if [ $RC != 0 ]; then
        /sbin/modprobe g_mass_storage stall=0 luns=2 removable=1,1
        RC=$?
    fi
fi

if [ $RC != 0 ]; then
    logger "$0: failed to install g_file_storage or g_mass_storage"
    exit 1
fi

sleep 1
/bin/grep $MODE -e idle > /dev/null
if [ $? = 0 ]; then
    logger "$0: usb cable detached after module change"
    # make sure we don't have devices in there
    echo '' > $LUN0
    echo '' > $LUN1
    exit 1
fi

initctl emit --no-wait G_FILE_STORAGE_READY

if [ $# -gt 1 ]; then
    logger "$0: only one argument supported"
    exit 1
fi

# check first if the card(s) are not used
grep -q "^$1" /proc/swaps
if [ $? = 0 ]; then
    logger "$0: $1 is in use for swap"
    exit 1
fi
grep -q "^$1" /proc/mounts
if [ $? = 0 ]; then
    logger "$0: $1 is in use"
    exit 1
fi

# check first if the card(s) are already shared
if [ $# = 1 ]; then
    FOUND=0
    if grep -q $1 $LUN0; then
        FOUND=1
    fi
    if grep -q $1 $LUN1; then
        FOUND=1
    fi
    if [ $FOUND = 1 ]; then
        logger "$0: $1 is already USB-shared"
        exit 0
    fi
fi

if [ $# = 1 ]; then
    STR=`cat $LUN0`
    if [ "x$STR" = "x" ]; then
        echo $1 > $LUN0
    else
        echo $1 > $LUN1
    fi
fi

exit 0
