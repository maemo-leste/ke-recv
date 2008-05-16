/**
  @file fat-tools.h
  Some FAT-related utilities.

  This file is part of ke-recv.

  Copyright (C) 2004-2008 Nokia Corporation. All rights reserved.

  Author: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>

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

#ifndef FAT_TOOLS_H_
#define FAT_TOOLS_H_

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* character codes < 128 are invalid, except for these: */
#define INVALID_FAT_CHARS "\\/:*?\"<>|"

/** Check a FAT name
    @param n name to check
    @return 0 if valid, -1 if too long, -2 if illegal characters.
*/
int valid_fat_name(const char* n);

#ifdef DEBUG
/** Function for testing valid_fat_name() */
void test_valid_fat_name(void);
#endif

#ifdef __cplusplus
}
#endif
#endif /* FAT_TOOLS_H_ */
