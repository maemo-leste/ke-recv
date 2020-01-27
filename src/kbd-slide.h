/**
  @file kbd-slide.h
  Code concerning state of the slide keyboard.

  This file is part of ke-recv.

  Copyright (C) 2020 Arthur Demchenkov <spinal.by@gmail.com>

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

#ifndef KBD_SLIDE_H_
#define KBD_SLIDE_H_

#ifdef __cplusplus
extern "C" {
#endif

void kbd_slide_monitor_start(void);
void kbd_slide_monitor_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* KBD_SLIDE_H_ */
