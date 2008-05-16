#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2006-2007 Nokia Corporation. All rights reserved.
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
  exit 0
fi
