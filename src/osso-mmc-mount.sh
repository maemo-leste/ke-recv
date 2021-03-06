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

BLS=/etc/default/osso-mmc-blacklist.sh

PDEV=$1  ;# preferred device (partition)
MP=$2    ;# mount point
FS=$3    ;# fstype

# hook for blacklist etc. Shall care for logger and exit 0, in case
test -x $BLS && source $BLS

grep "$PDEV " /proc/mounts > /dev/null
if [ $? = 0 ]; then
  logger "$0: $PDEV is already mounted"
  exit 0
fi

if [ ! -d $MP ]; then
  mkdir -p $MP
fi

# time limited check
#/sbin/dosfsck -I -n -T 10 $PDEV
#if [ $? != 0 ]; then
#  logger "$0: $PDEV is corrupt, trying to mount it read-only"
#  mmc-mount $PDEV $MP ro
#  if [ $? = 0 ]; then
#    logger "$0: $PDEV mounted read-only"
#    exit 2
#  else
#    logger "$0: Couldn't mount $PDEV read-only"
#    exit 1
#  fi
#fi

mmc-mount $PDEV $MP rw $FS
RC=$?
logger "$0: mounting $PDEV read-write fs $FS to $MP, rc: $RC"

if [ $RC = 0 ]; then
  # create some special directories for user's partition
  if [ "x$MP" = "x/home/user/MyDocs" -a -w $MP ]; then
    # use global folder names
    USERDIRS="/home/user/.config/user-dirs.dirs"
    if [ -f "$USERDIRS" ]; then
      HOME='/home/user'
      source "$USERDIRS"
      mkdir -p "$XDG_DOCUMENTS_DIR" 
      mkdir -p "$XDG_PICTURES_DIR"
      mkdir -p "$XDG_MUSIC_DIR"
      mkdir -p "$XDG_VIDEOS_DIR" 
      mkdir -p "$NOKIA_CAMERA_DIR"
    else
      # fallback
      for d in .sounds .videos .documents .images .camera; do
        mkdir -p $MP/$d
      done
    fi
    touch $MP
  elif [ "x$MP" = "x/home/user/MyDocs" ]; then
    logger "$0: '$MP' is not writable"
  elif [ "x$MP" = "x/media/mmc1" -a -w $MP ]; then
    # use global folder names
    USERDIRS="/home/user/.config/user-dirs.dirs"
    if [ -f "$USERDIRS" ]; then
      HOME='/home/user'
      source "$USERDIRS"
      mkdir -p "$NOKIA_MMC_CAMERA_DIR"
    fi
  elif [ "x$MP" = "x/media/mmc1" ]; then
    logger "$0: '$MP' is not writable"
  fi
fi

exit $(($RC != 0))
