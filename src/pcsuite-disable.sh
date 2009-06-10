#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2008-2009 Nokia Corporation. All rights reserved.
#
# Author: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>
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

/sbin/lsmod | grep g_nokia > /dev/null
if [ $? = 0 ]; then
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

    sleep 2
    /sbin/rmmod g_nokia
    if [ $? != 0 ]; then
        logger "$0: failed to rmmod g_nokia!"
        exit 1
    fi
fi

RC=0
/sbin/lsmod | grep g_nada > /dev/null
if [ $? != 0 ]; then
    /sbin/modprobe g_nada
    RC=$?
fi

if [ $RC != 0 ]; then
    logger "$0: failed to install g_nada"
    exit 1
fi

exit 0
