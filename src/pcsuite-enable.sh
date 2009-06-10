#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2008-2009 Nokia Corporation. All rights reserved.
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

/sbin/lsmod | grep g_nada > /dev/null
if [ $? = 0 ]; then
    logger "$0: removing g_nada"
    /sbin/rmmod g_nada
fi

RC=0
/sbin/lsmod | grep g_nokia > /dev/null
if [ $? != 0 ]; then
    /sbin/modprobe g_nokia
    RC=$?
fi

if [ $RC != 0 ]; then
    logger "$0: failed to install g_nokia"
    # put g_nada back
    /sbin/modprobe g_nada
    if [ $? != 0 ]; then
      logger "$0: failed to install g_nada back"
    fi
    exit 1
fi

# TODO: wait for device files
sleep 1

initctl emit --no-wait G_NOKIA_READY

# TODO: wait for daemons
sleep 1

OBEXD_PID=`pidof obexd`
if [ $? != 0 ]; then
    logger "$0: obexd is not running"
else
    kill -USR1 $OBEXD_PID
    logger "$0: sent SIGUSR1 to obexd"
fi

SYNCD_PID=`pidof syncd`
if [ $? != 0 ]; then
    logger "$0: failed to get syncd's PID"
    exit 1
fi

kill -USR1 $SYNCD_PID
logger "$0: sent SIGUSR1 to syncd"

exit 0
