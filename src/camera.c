/**
  @file camera.c
  Code concerning state of the camera.
      
  This file is part of ke-recv.

  Copyright (C) 2004-2007 Nokia Corporation. All rights reserved.

  Contact: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>

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

#include <sys/types.h>
#include <linux/types.h>
#define _LINUX_TIME_H
#define __user
#include <linux/videodev2.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include "camera.h"

#define VIDEO_DEVICE "/dev/video0"
#define WIDTH 172
#define HEIGHT 128
#define FORMAT V4L2_PIX_FMT_YVU420

gboolean camera_is_open(void)
{
        int fd;
        gboolean ret;

        fd = open(VIDEO_DEVICE, O_RDWR);

        if (fd < 0) {
            ret = TRUE;
        }

        else {
            struct v4l2_format format;

            format.fmt.pix.width = WIDTH;
            format.fmt.pix.height = HEIGHT;
            format.fmt.pix.pixelformat = FORMAT;
            format.fmt.pix.field = V4L2_FIELD_INTERLACED;
            format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
                ret = TRUE;
            }

            else {
                ret = FALSE;
            }

            close(fd);
        }

        return ret;
}
