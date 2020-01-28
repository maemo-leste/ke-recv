/* ========================================================================= *
 * Copyright (C) 2004-2006 Nokia Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * File: swap_mgr.c
 *
 * Contact: Leonid Moiseichuk <leonid.moiseichuk@nokia.com>
 *
 * Description:
 *    Swap file management.
 *
 * History:
 *
 * 05-Jan-2006 Leonid Moiseichuk
 * - finished testing in different modes, slighly recalibrated.
 *
 * 15-Dec-2006 Leonid Moiseichuk
 * - added support of several swap files/partitions using /proc/swaps
 *
 * 04-Aug-2006 Leonid Moiseichuk, bug 36879
 * - added _XOPEN_SOURCE macro to support strict ansi mode for some compilers.
 *
 * 31-May-2006 Leonid Moiseichuk, again bug 30541
 * - added delay to allow mmc controller to finalize IO operations.
 * - optimization of number of passes and speed.
 *
 * 24-May-2006 Leonid Moiseichuk
 * - update swap creation procedure according to bug 30541.
 *
 * 22-May-2006 Leonid Moiseichuk
 * - update swap file closing sequence according to bug 24368 investigation results.
 *
 * 12-May-2006 Leonid Moiseichuk
 * - revert back to asynchronous mode because that is faster.
 * - make sync calls every specified period.
 *
 * 11-May-2006 Leonid Moiseichuk
 * - swap creation now synchronous.
 *
 * 13-Mar-2006 Leonid Moiseichuk
 * - fixed initilization issue in swap_init_header reported by Kimmo Hamalainen.
 * - improved robustness of swap_create
 * - time to create swap file reduced from 76s to 55s for 32 MB swap file (maximum possible).
 * - added callback for swap file creation for UI.
 *
 * 28-Feb-2006 Leonid Moiseichuk
 * - added swap_enabled function to check is swap enabled or not.
 * - file renamed to swap_mgr according to letter.
 *
 * 27-Feb-2006 Leonid Moiseichuk
 * - added 3 service functions for minimal and maximal size.
 *
 * 24-Feb-2006 Leonid Moiseichuk
 * - initial version created.
 * ========================================================================= */

/* ========================================================================= *
 * Includes
 * ========================================================================= */

#define __USE_GNU
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/swap.h>

#include "swap_mgr.h"

/* ========================================================================= *
 * Definitions.
 * ========================================================================= */

/* Some files are necessary for support swap */
#define MEMINFO_PATH       "/proc/meminfo"
#define SWAPS_PATH         "/proc/swaps"
#define HIGH_LIMIT_PATH    "/proc/sys/vm/lowmem_notify_high"
#define HIGH_DECAY_PATH    "/proc/sys/vm/lowmem_nr_decay_pages"

/* Timeout that necessary to flush current write operations on storage device.    */
/* Microseconds per one page IO using NAND: 2 ms for write (2 ms need for erase). */
#define SWAP_CREATE_STORAGE_PAGE_IO (2 * 1000)

/* Compile-time array capacity calculation */
#define CAPACITY(a)     (sizeof(a) / sizeof(*a))

/* Correct division of 2 unsigned values */
#define DIVIDE(a,b)     (((a) + ((b) >> 1)) / (b))


/* Type for constant string */
typedef const char* CPSZ;

typedef struct
{
   const char* name;    /* /proc/meminfo parameter with ":" */
   unsigned    data;    /* loaded value                     */
} MEMINFO;


/* We support swap file version 1.0 */
/* WARNING: the related code is copy-pasted from busybox */

#define SWAP_SIGNATURE        "SWAPSPACE2"
#define SWAP_SIGNATURE_SIZE   (sizeof(SWAP_SIGNATURE) - sizeof(char))

typedef struct
{
   char         bootbits[1024];    /* Space for disklabel etc. */
   unsigned int version;
   unsigned int last_page;
   unsigned int nr_badpages;
   unsigned int padding[125];
   unsigned int badpages[1];
} SWAP_HEADER;


/* ========================================================================= *
 * Local .
 * ========================================================================= */


/* ========================================================================= *
 * Local methods.
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * seek_word -- Seeks and cuts the word in some line, space separated.
 * parameters: pointer to storage, flag is first/next.
 * returns: pointer to word begin.
 * ------------------------------------------------------------------------- */
static char* seek_word(char* line, int is_next)
{
   if ( line )
   {
      /* Shall we skip current word? */
      char* word = (is_next ? line + strlen(line) + 1 : line);

      /* Skip all spaces */
      while (*word && isspace(*word))
         word++;

      /* Check end of line, contunue only if we have some non-space */
      if ( *word )
      {
         /* Re-use line as a pointer to end of word and line continuation */
         line = word + 1;

         /* Skip all non-space characters */
         while (*line && !isspace(*line))
            line++;

         /* We reach \0 or space = put \0 and exit */
         *line = 0;

         /* Word is found */
         return word;
      }
   }

   /* Looks we have no anything more */
   return NULL;
} /* seek_word */

/* ------------------------------------------------------------------------- *
 * swap_location -- Queries about swap file mount point (location).
 * parameters: nothing.
 * returns: NULL or string with location.
 * ------------------------------------------------------------------------- */
static CPSZ swap_location(void)
{
   static CPSZ location = NULL;

   /* Did we already asked about swap location? */
   if ( !location )
   {
      location = getenv(SWAP_VAR);

      /* In case of location is not known lets assign it to empty string "" */
      if ( !location )
         location = "";
   }

   /* Location is known or we already asked about it */
   return (*location ? location : NULL);
} /* swap_location */

/* ------------------------------------------------------------------------- *
 * swap_path -- Generate osso swap file full path.
 * parameters: data and size to store path.
 * returns: NULL or string with path passed in parameters.
 * ------------------------------------------------------------------------- */
static char* swap_path(char* data, int size)
{
   CPSZ location = swap_location();
   int  retcode;

   /* Checking for parameters and status */
   if ( !data )
      return NULL;

   if ( size <= 0 )
      return NULL;

   if ( !location )
      return NULL;

   /* Create the full file path */
   retcode = snprintf(data, size, "%s/%s", location, SWAP_NAME);

   /* Check the result */
   if (retcode > 0 && retcode < size)
   {
      /* Success */
      return data;
   }
   else
   {
      /* Actually no space or other kind of issie */
      *data = 0;
      return NULL;
   }
} /* swap_path */

/* ------------------------------------------------------------------------- *
 * swap_fs_free -- Detects available size on file system at SWAP_VAR
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - validated available file system size.
 * ------------------------------------------------------------------------- */
static unsigned swap_fs_free(void)
{
   CPSZ location = swap_location();
   struct statfs sfs;

   /* Is swap permitted? */
   if ( !location )
      return 0;

   /* Obtain data from the file system and returns size available to non-superuser */
   return (statfs(location, &sfs) ? 0 : swap_available() + sfs.f_bsize * sfs.f_bavail);
} /* swap_fs_free */


/* ------------------------------------------------------------------------- *
 * swap_read_proc -- Load value from specified /proc file and return it.
 * parameters:
 *    - path to proc file.
 *    - default value if file is not available.
 * returns: value loaded from the proc file or default value.
 * ------------------------------------------------------------------------- */
static unsigned swap_read_proc(CPSZ proc, unsigned defval)
{
   FILE* fp = fopen(proc, "rt");

   if ( fp )
   {
      unsigned value;

      /* trying to load value from file */
      if (fscanf(fp, "%u", &value) <= 0)
         value = defval;

      fclose(fp);

      return value;
   }
   else
   {
      return defval;
   }
} /* swap_read_proc */

/* ------------------------------------------------------------------------- *
 * swap_read_meminfo -- open meminfo file and read specified values.
 * parameters:
 *    vals - array of values to be handled.
 *    size - size of vals array.
 * returns: number of sucessfully loaded values.
 * ------------------------------------------------------------------------- */
static unsigned swap_read_meminfo(MEMINFO* vals, unsigned size)
{
   unsigned counter = 0;
   FILE*    meminfo = fopen(MEMINFO_PATH, "rt");

   if ( meminfo )
   {
      char line[256];

      /* Load lines from file until we not define all variables */
      while (counter < size && fgets(line, CAPACITY(line), meminfo))
      {
         unsigned idx;

         /* Search and setup parameter */
         for (idx = 0; idx < size; idx++)
         {
            const unsigned length = strlen(vals[idx].name);

            /* Compare first bytes - shall be equal */
            if ( 0 == memcmp(line, vals[idx].name, length) )
            {
               /* Parameter has a format SomeName:\tValue, we expect that MEMINFO::name contains ":" */
               vals[idx].data = (unsigned)strtoul(line + length + 1, NULL, 0);
               counter++;
               break;
            }
         }
      } /* while have data */

      fclose(meminfo);
   }

   return counter;
} /* swap_read_meminfo */

/* ------------------------------------------------------------------------- *
 * swap_read_usage -- Get specified swap file usage by checking /proc/swaps.
 * parameters: IN swap_name, OUT size [KB], OUT used [KB].
 * returns: zero - no swap_name found, 1 swap_name detected.
 * ------------------------------------------------------------------------- */
static unsigned swap_read_usage(const char* swap_name, unsigned* size, unsigned* used)
{
   unsigned found = 0;

   /* Clean-up size and used */
   if (size)
      *size = 0;

   if (used)
      *used = 0;

   /* swap_name may be NULL in case of swapping is prohibited */
   if ( swap_name )
   {
      FILE* swaps = fopen(SWAPS_PATH, "rt");

      if ( swaps )
      {
         char line[256];

         /* Load lines from file until not found */
         while ( fgets(line, CAPACITY(line), swaps) )
         {
            /* Format of /proc/swaps is the following                                              */
            /* Filename                                Type            Size    Used    Priority    */
            /* /media/mmc2/.swap                       file            32748   0       -1          */
            char* cursor = seek_word(line, 0);

            if (cursor && !strcmp(swap_name, cursor))
            {
               /* Skip Type: file or partition */
               cursor = seek_word(cursor, 1);

               /* Get the Size */
               cursor = seek_word(cursor, 1);
               if ( size )
                  *size = (unsigned)strtoul(cursor, NULL, 0);

               /* Get the Used */
               cursor = seek_word(cursor, 1);
               if ( used )
                  *used = (unsigned)strtoul(cursor, NULL, 0);

               /* Exit from loop */
               found = 1;
               break;
            }
         } /* while */

         fclose(swaps);
      }
   }

   return found;
} /* swap_read_usage */

/* ------------------------------------------------------------------------- *
 * swap_total_ram -- Queries total available amount of memory.
 * parameters: nothing.
 * returns: amount of available memory in system [bytes].
 * ------------------------------------------------------------------------- */
static unsigned swap_total_ram(void)
{
   static unsigned total_ram = 0;

   /* Is this value is initialized? */
   if ( !total_ram )
   {
      MEMINFO total = { "MemTotal:", 1 };

      if (1 == swap_read_meminfo(&total, 1))
         total_ram = ((total.data << 10) + SWAP_GRANULARITY - 1) & ~( SWAP_GRANULARITY - 1);
   }

   return total_ram;
} /* swap_total_ram */

/* ------------------------------------------------------------------------- *
 * swap_init_header -- Initializes swap file signature page.
 * parameters: page, swap size.
 * returns: none.
 * ------------------------------------------------------------------------- */
static void swap_init_header(void* page, unsigned swap_size)
{
   const unsigned size = getpagesize();
   SWAP_HEADER* header = (SWAP_HEADER*)page;

   /* Clean up and setup */
   memset(page, 0, size);
   memcpy((char*)page + (size - SWAP_SIGNATURE_SIZE), SWAP_SIGNATURE, SWAP_SIGNATURE_SIZE);

   header->version     = 1;
   header->last_page   = (swap_size / size) - 1;
   header->nr_badpages = 0;
} /* swap_init_header */

/* ========================================================================= *
 * Methods.
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * swap_permitted -- Detects is swap permitted to be created and activated.
 * parameters: nothing.
 * returns: 1 if permitted and 0 if not.
 * ------------------------------------------------------------------------- */
unsigned swap_permitted(void)
{
   return (NULL != swap_location());
} /* swap_permitted */


/* ------------------------------------------------------------------------- *
 * swap_available -- Returns amount of memory available in swap file.
 * parameters: nothing.
 * returns:
 *    0     - swap file is not created or permitted
 *    value - amount of available swap memory in bytes.
 * ------------------------------------------------------------------------- */
unsigned swap_available(void)
{
   char path[256];
   struct stat buf;

   /* Check the availability of swap file */
   if ( !swap_path(path, sizeof(path)) )
      return 0;

   /* Try to swap file access */
   if ( stat(path, &buf) )
      return 0;

   /* Test the access rights, S_ISBLK(buf.st_mode) not checked because swap may be in use */
   if ( !S_ISREG(buf.st_mode) )
      return 0;

   /* Looks Ok, lets return his size */
   return (unsigned)(buf.st_size);
} /* swap_available */

/* ------------------------------------------------------------------------- *
 * swap_validate_size -- Validates proposed size of swap file.
 * parameters: proposed size in bytes.
 * returns: validated size in bytes or 0 in case of error.
 * ------------------------------------------------------------------------- */
unsigned swap_validate_size(unsigned size)
{
   /* Amount of RAM available on board */
   unsigned ram_size;

   /* Check the minimal level */
   if (size < SWAP_MINIMUM)
      return 0;

   /* Align proposed size and obtain amount of available RAM */
   size    &= ~(SWAP_GRANULARITY - 1);
   ram_size = swap_total_ram();

   /* Check for maximal level */
   if (size > ram_size)
      return ram_size;

   /* The proposed size accepted as a resulting size */
   return size;
} /* swap_validate_size */


/* ------------------------------------------------------------------------- *
 * swap_minimal_size -- Detects available space on file system at
 *       SWAP_VAR and generate minimal size of swap file.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - validated minimal swap size in bytes (SWAP_MINIMUM).
 * ------------------------------------------------------------------------- */
unsigned swap_minimal_size(void)
{
   return (swap_fs_free() < SWAP_MINIMUM ? 0 : SWAP_MINIMUM);
} /* swap_minimal_size */

/* ------------------------------------------------------------------------- *
 * swap_automatic_size -- Detects available size on file system at
 *       SWAP_VAR and generate possible size of swap file.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - validated recommended swap size in bytes if everything is Ok.
 * ------------------------------------------------------------------------- */
unsigned swap_automatic_size(void)
{
   /* Get the half of free space and validate it */
   return swap_validate_size(swap_fs_free() >> 1);
} /* swap_automatic_size */

/* ------------------------------------------------------------------------- *
 * swap_maximal_size -- Detects available space on file system at
 *       SWAP_VAR and generate maximal size of swap file.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - validated maximal swap size in bytes.
 * ------------------------------------------------------------------------- */
unsigned swap_maximal_size(void)
{
   /* Get the free space and validate it */
   return swap_validate_size(swap_fs_free());
} /* swap_maximal_size */

/* ------------------------------------------------------------------------- *
 * swap_size_granularity -- Returns swap size granularity.
 * parameters: nothing.
 * returns:
 *    0 if not permitted or no space or error.
 *    value - granularity of swap file size.
 * ------------------------------------------------------------------------- */
unsigned swap_size_granularity(void)
{
   return (NULL == swap_location() ? 0 : SWAP_GRANULARITY);
} /* swap_size_granularity */


/* ------------------------------------------------------------------------- *
 * swap_create -- Create and format swap file.
 * parameters: size of swap file in bytes and callback function (may be NULL).
 * returns: 0 on success or errno (callback return code) in case of error.
 * ------------------------------------------------------------------------- */
int swap_create(unsigned size, SWAP_CREATE_CALLBACK callback)
{
   const int page_size = getpagesize();

   char         path[256];
   char         page[page_size];
   int          file;
   int          retcode = 0;
   unsigned     counter;

   /* Obtain path and validate swap permissions */
   if ( !swap_path(path, sizeof(path)) )
      return EACCES;

   /* Validate swap file size */
   size = swap_validate_size(size);
   if ( size )
      counter = (size / page_size) - 1;
   else
      return EINVAL;

   /* Open the file, O_ASYNC PROHIBITED because swap can be incorrectly created */
   file = open(path, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR | S_IWUSR);
   if ( file < 0 )
      return errno;

   /* Initialize first (header) page */
   swap_init_header(page, size);
   if (page_size == write(file, page, page_size) && 0 == fsync(file))
   {
      unsigned  index;

      /* Header page stored successfuly */
      usleep(SWAP_CREATE_STORAGE_PAGE_IO);

      /* Clean the page before writing [optional, simplified dd + mkswap debugging]*/
      memset(page, 0, sizeof(page));

      /* Fill all pages with some trash */
      for (index = 1; index <= counter; index++)
      {
         /* Should we continue or User decide to stop? */
         if (callback && 0 != (retcode = callback(index, counter)))
            break;

         /* Check quality of writing swap file */
         if (page_size == write(file, page, page_size))
         {
            /* Synchronize every 64 pages of swap file */
            if (0 != (index & 63) || 0 == fsync(file))
            {
               /* Waiting to finalization storage IO operations */
               usleep(SWAP_CREATE_STORAGE_PAGE_IO);
               continue;
            }
         }

         /* No space or other issue -> force to exit */
         retcode = errno;
         break;
      } /* for */
   }
   else
   {
      /* Save the error code as a result */
      retcode = errno;
   }

   /* Check the quality of swap file */
   if (0 == retcode)
   {
      /* Flush changes and close file */
      if (0 == fsync(file) && 0 == close(file))
      {
         usleep(SWAP_CREATE_STORAGE_PAGE_IO * 10);
         return 0;
      }
      else
      {
         /* Save error code to report */
         retcode = errno;
      }
   }

   /* Bad blocks or error during header update. Delete first and than close. */
   unlink(path);
   close(file);

   /* We should report as much valid error code */
   return retcode;
} /* swap_create */

/* ------------------------------------------------------------------------- *
 * swap_delete -- Delete (unmounted) swap file.
 * parameters: nothing.
 * returns: 0 on success or errno.
 * ------------------------------------------------------------------------- */
int swap_delete(void)
{
   char path[256];

   /* Obtain path and validate swap permissions */
   if ( !swap_path(path, sizeof(path)) )
      return EACCES;

   /* Remove swap file and return the error code */
   return (unlink(path) ? errno : 0);
} /* swap_delete */


/* ------------------------------------------------------------------------- *
 * swap_enabled -- Checks, enabled swapping currently or not.
 * parameters: nothing.
 * returns: 0 - swap is not enabled, 1 - swap is enabled.
 * ------------------------------------------------------------------------- */
unsigned swap_enabled(void)
{
   char path[256];
   /* Obtain path, check if it enabled */
   return (swap_path(path, sizeof(path)) ? swap_read_usage(path, NULL, NULL) : 0);
} /* swap_enabled */

/* ------------------------------------------------------------------------- *
 * swap_can_switch_off -- Calculate can we safely switch swap off or not.
 * parameters: nothing.
 * returns: 0 we have to close applications, 1 - we can stop swap safely.
 * ------------------------------------------------------------------------- */
unsigned swap_can_switch_off(void)
{
   /* Getting all necessary information to make a decision */
   MEMINFO info[] =
      {
         { "MemFree:",     0 },  /* 0 */
         { "Buffers:",     0 },  /* 1 */
         { "Cached:",      0 },  /* 2 */
         { "SwapTotal:",   0 },  /* 3 */
         { "SwapFree:",    0 }   /* 4 */
      }; /* info */
   unsigned high_limit;
   unsigned high_decay;

   /* Working variables */
   char     path[256];
   unsigned swap_size;
   unsigned swap_used;

   unsigned other_swaps_free;
   unsigned mem_free;
   unsigned post_free;
   unsigned safely_free;

   /* First, obtain path to swap file */
   if ( !swap_path(path, sizeof(path)) )
      return 1;

   /* Second, check if allowed swap file really enabled */
   if (!swap_read_usage(path, &swap_size, &swap_used) || !swap_used)
      return 1;

   /* Load the values from meminfo file without checking */
   swap_read_meminfo(info, CAPACITY(info));

   /* Check how much memory we have in other swap files */
   other_swaps_free = info[4].data - (swap_size - swap_used);
   /* Increase usage in external swap because device can be rebooted by watchdog */
   /* and we must have spare. The next statement are quite empirical but works.  */
   swap_used *= 2;
   if (other_swaps_free > swap_used)
      return 1;

   /* Take into account free space in other swap files */
   swap_used -= other_swaps_free;

   /* Calculate amount of free memory (RAM) */
   mem_free = info[0].data + info[1].data + info[2].data;

   /* Rought check of possibility to free */
   if ( swap_used >= mem_free )
      return 0;

   /* Calculate amount of free memory after swap is turned off */
   post_free = (mem_free - swap_used) << 10;

   /* Calculate how much memory we must have available to safely work */
   high_limit  = swap_read_proc(HIGH_LIMIT_PATH, 97);     /* 97 hardcoded in kernel    */
   high_decay  = swap_read_proc(HIGH_DECAY_PATH, 256);    /* 1MB spare for decay pages */
   safely_free = DIVIDE(swap_total_ram() * (100 - high_limit), 100) + high_decay * getpagesize();

   /* Last check for free memory */
   if (post_free < safely_free)
      return 0;

   /* We have a swap file and can turn it off safely */
   return 1;
} /* swap_can_switch_off */

/* ------------------------------------------------------------------------- *
 * swap_switch_on -- Attempt to swapon swap file.
 * parameters: nothing.
 * returns: 0 on success or errno.
 * ------------------------------------------------------------------------- */
int swap_switch_on(void)
{
   char path[256];

   /* Obtain path and validate swap permissions */
   if ( !swap_path(path, sizeof(path)) )
      return EACCES;

   /* Enable swap and return the error code */
   return (swapon(path, 0) ? errno : 0);
} /* swap_switch_on */

/* ------------------------------------------------------------------------- *
 * swap_switch_off -- Attempt to swapoff swap file.
 * parameters: nothing.
 * returns: 0 on success or errno.
 * ------------------------------------------------------------------------- */
int swap_switch_off(void)
{
   char path[256];

   /* Obtain path and validate swap permissions */
   if ( !swap_path(path, sizeof(path)) )
      return EACCES;

   /* Disable swap and return the error code */
   return (swapoff(path) ? errno : 0);
} /* swap_switch_off */


/* ========================================================================= *
 * Main method for testing purposes.
 * ========================================================================= */
#ifdef TEST

static int swap_create_callback(unsigned current_page, unsigned latest_page)
{
   if (0 == (current_page % (latest_page / 100)))
   {
      printf ("%4u/%4u pages written\r", current_page, latest_page);
      fflush(stdout);
   }
   /* Lets continue always */
   return 0;
} /* swap_create_callback */


int main(int argc, CPSZ argv[])
{
   unsigned size;
   CPSZ ops = (1 == argc ? "cd" : argv[1]);

   printf ("swap methods tester. build %s %s.\n", __DATE__, __TIME__);

   /* testing for permission */
   if ( swap_permitted() )
   {
      printf ("1. swap usage is permitted\n");
   }
   else
   {
      printf ("1. swap usage is NOT permitted. define location in variable %s\n", SWAP_VAR);
      return -1;
   }

   size = swap_available();
   if ( size )
   {
      printf ("2. swap file available: %u MB\n", size >> 20);
   }
   else
   {
      printf ("2. no swap file available\n");
   }

   printf ("3. swap_validate_size testing:\n");
   printf ("- %u --> %u\n", 10, swap_validate_size(10));
   printf ("- %u --> %u\n", 32123123, swap_validate_size(32123123));
   printf ("- %u --> %u\n", 3 << 30, swap_validate_size(3 << 30));

   printf ("4. swap file size granularity: %u MB\n", swap_size_granularity() >> 20);
   printf ("5. minimal swap file size: %u MB\n", swap_minimal_size() >> 20);
   printf ("6. recommended size of swap file: %u MB\n", swap_automatic_size() >> 20);
   printf ("7. maximal swap file size: %u MB\n", swap_maximal_size() >> 20);

   if ( strchr(ops, 'c') )
   {
      const unsigned size = (3 == argc ? strtoul(argv[2], NULL, 0) << 20 : SWAP_MINIMUM);

      if ( swap_create(size, swap_create_callback) )
      {
         printf ("8. swap file IS NOT created\n");
      }
      else
      {
         printf ("8. swap file is created\n");
      }

      if ( swap_switch_on() )
      {
         printf ("9. swap file IS NOT switched on\n");
      }
      else
      {
         printf ("9. swap file switched on\n");
      }
   }

   if ( swap_enabled() )
   {
      printf ("a. swapping is enabled in system now\n");
   }
   else
   {
      printf ("a. swapping is NOT enabled in system now\n");
   }

   if ( swap_can_switch_off() )
   {
      printf ("b. swap file can be switched off\n");
   }
   else
   {
      printf ("b. swap file CAN NOT be switched off\n");
   }

   if ( strchr(ops, 'd') )
   {
      if ( swap_switch_off() )
      {
         printf ("c. swap file IS NOT switched off\n");
      }
      else
      {
         printf ("c. swap file switched off\n");
      }

      if ( swap_delete() )
      {
         printf ("d. swap file IS NOT deleted\n");
      }
      else
      {
         printf ("d. swap file deleted\n");
      }
   }

   return 0;
} /* main */

#endif

/* ---------------------------[ end of file swap_mgr.c ]-------------------------- */

