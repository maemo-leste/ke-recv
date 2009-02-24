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

/sbin/lsmod | grep g_file_storage > /dev/null
if [ $? = 0 ]; then
    echo "$0: removing g_file_storage"
    initctl emit G_FILE_STORAGE_REMOVE
    /sbin/rmmod g_file_storage
fi

/sbin/lsmod | grep g_nokia > /dev/null
if [ $? != 0 ]; then
    /sbin/modprobe g_nokia
    RC=$?
fi

if [ $RC != 0 ]; then
    echo "$0: failed to install g_nokia"
    exit 1
else
    # TODO: remove the sleep when the wait is in place
    sleep 2
fi

initctl emit --no-wait G_NOKIA_READY

exit 0
