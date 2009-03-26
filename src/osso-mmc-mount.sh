#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2005-2009 Nokia Corporation. All rights reserved.
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

# Return codes:
# 0 - mounted read-write
# 1 - could not mount
# 2 - mounted read-only

PDEV=$1  ;# preferred device (partition)
MP=$2    ;# mount point

grep "$PDEV " /proc/mounts > /dev/null
if [ $? = 0 ]; then
  logger "$0: $PDEV is already mounted"
  exit 0
fi

if [ ! -d $MP ]; then
  mkdir -p $MP
fi

if ! [ $PDEV = /dev/mmcblk0 -o $PDEV = /dev/mmcblk0 ]; then
  # check the FAT magic number
  PNUM=$(echo $PDEV | sed "s#/dev/mmcblk[01]p##")
  DEV=$(echo $PDEV | sed "s#p[1234]##")
  PID=$(sfdisk -c $DEV $PNUM)
  if [ $PID != b -a $PID != c ]; then
    logger "$0: $PDEV is not FAT32"
    exit 1
  fi
fi

# time limited check
/sbin/dosfsck -n -T 10 $PDEV
if [ $? != 0 ]; then
  logger "$0: $PDEV is corrupt, trying to mount it read-only"
  mmc-mount $PDEV $MP ro
  if [ $? = 0 ]; then
    logger "$0: $PDEV mounted read-only"
    exit 2
  else
    logger "$0: Couldn't mount $PDEV read-only"
    exit 1
  fi
fi

mmc-mount $PDEV $MP rw
RC=$?
logger "$0: mounting $PDEV read-write to $MP, rc: $RC"

if [ $RC = 0 ]; then
  # create some special directories for user's partition
  if [ "x$MP" = "x/home/user/MyDocs" -a -w $MP ]; then
    for d in .sounds .videos .documents .images .camera; do
      mkdir -p $MP/$d
    done
  else
    logger "$0: '$MP' is not writable"
  fi
fi

exit $(($RC != 0))
