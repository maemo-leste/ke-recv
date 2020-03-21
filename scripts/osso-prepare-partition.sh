#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2006-2009 Nokia Corporation. All rights reserved.
#
# Author: Kimmo H�m�l�inen <kimmo.hamalainen@nokia.com>
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

echo "$0: zeroing beginning of $DEV, to clear partition table"
dd if=/dev/zero of=$DEV bs=512 count=1 > /dev/null

sfdisk -q -D $DEV << EOF
,,b
EOF
if [ $? != 0 ]; then
  echo "$0: could not make the partition"
  exit 1
else
  echo "$0: successfully created the partition"
  # make the kernel to detect the new partition table
  sfdisk -R $DEV
  # magic sleep to give time to udev (would be better to wait
  # for the partition file in a loop)
  sleep 1
  exit 0
fi
