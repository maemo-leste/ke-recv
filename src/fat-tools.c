/**
  @file fat-tools.c
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

#include "fat-tools.h"

/** Check a FAT name
   @param n name to check
   @return 0 if valid, -1 if too long, -2 if illegal characters.
*/
int valid_fat_name(const char* n)
{
      /*  static const char bad_start_bytes[] = {0xE5, 0x20, 0}; */
        static const char bad_start_bytes[] = {0xE5, 0};
        static const char bad_name_bytes[] = {0x22, 0x2A, 0x2B,
                0x2C, 0x2E, 0x2F, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
                0x5B, 0x5C, 0x5D, 0x7C, 0};
        int i = 0, j = 0;
        /* check for illegal first character */
        if (n[0] == 0) {
                return -2;
        }
        for (i = 0; bad_start_bytes[i] != 0; ++i) {
                if (n[0] == bad_start_bytes[i]) {
                        return -2;
                }
        }
        /* check for illegal characters in the name */
        for (i = 0; n[i] != '\0'; ++i) {
		if (i > 10) {
			return -1;
		}
                if (strchr(INVALID_FAT_CHARS, n[i]) != NULL) {
                        return -2;
                }
                for (j = 0; bad_name_bytes[j] != 0; ++j) {
                        if (n[i] == bad_name_bytes[j]) {
                                return -2;
                        }
                }
                if (n[i] < 0x20 && !(i == 0 && n[i] == 0x05)) {
                        return -2;
                }
                if (isalpha(n[i]) && islower(n[i])) {
                        /* lower case chars aren't allowed */
                        return -2;
                }
        }
        return 0;
}

#ifdef DEBUG
#define CHECK_FAIL if (valid_fat_name(buf) == 0) {\
	printf("'%s' should have failed\n", buf); exit(1); }
#define CHECK_SUCC if (valid_fat_name(buf) != 0) {\
	printf("'%s' should have succeeded\n", buf); exit(1); }
void test_valid_fat_name(void)
{
	char buf[13];
	/* illegal starts with a space */
	sprintf(buf, "            ");
	CHECK_FAIL;
	/* illegal, contains char < 0x20 */
	sprintf(buf, "A %c        ", 0x19);
	CHECK_FAIL;
	/* illegal, contains lower-case letters */
	sprintf(buf, "lower case ");
	CHECK_FAIL;
	/* illegal, contains one of forbidden bytes */
	sprintf(buf, "TEST%c      ", 0x3D);
	CHECK_FAIL;
	/* illegal, too long (12) */
	sprintf(buf, "TOO LONG VOL");
	CHECK_FAIL;
	/* legal, 0x05 is the first character */
	sprintf(buf, "%c          ", 0x05);
	CHECK_SUCC;
	/* illegal, 0x05 is not the first char */
	sprintf(buf, "TEST FAILS%c", 0x05);
	CHECK_FAIL;
	/* legal */
	sprintf(buf, "UPPER CASE ");
	CHECK_SUCC;
	/* legal */
	sprintf(buf, "504753 1234");
	CHECK_SUCC;
}

#endif /* DEBUG */
