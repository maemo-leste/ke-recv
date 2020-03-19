#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2006-2007 Nokia Corporation. All rights reserved.
# Copyright (C) 2012 Pali Rohár <pali.rohar@gmail.com>
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

DEV=$1
LABEL=$2
FSTYPE=$3

if [ -z "$FSTYPE" ]; then
  FSTYPE=vfat
fi

case $FSTYPE in
  vfat)
    PROG=/sbin/dosfslabel
    ;;
  ext2|ext3|ext4)
    PROG=/sbin/e2label
    ;;
  *)
    exit 1
esac

$PROG $DEV "$LABEL"
RESULT=$?
exit $RESULT
