/*
  Plastic Logic EPD project on MSP430

  Copyright (C) 2013 Plastic Logic Limited

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef INCLUDE_UTIL_H
#define INCLUDE_UTIL_H 1


#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <libpng-1.2.51/png.h>
#include "types.h"

#ifdef LOG_TAG
#define LOG(msg, ...) \
	do { printf("%-16s "msg"\n", LOG_TAG, ##__VA_ARGS__); } while (0)
#else
#define LOG(msg, ...)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef min
#define min(x,y)	( (x) < (y) ? (x) : (y) )
#endif
#ifndef max
#define max(x,y)	( (x) > (y) ? (x) : (y) )
#endif

#define DIV_ROUND_CLOSEST(x, divisor)(				\
	{							\
		(((x) + ((divisor) / 2)) / (divisor));		\
	}							\
)

#define CPU_CLOCK_SPEED_IN_HZ	20000000L
#if CPU_CLOCK_SPEED_IN_HZ < 1000000L
#error CPU_CLOCK_SPEED_IN_HZ assumed to be more than 1MHz in delay timer calculations
#endif

typedef uint8_t bool;

enum bool_opts {
	false = 0,
	true = 1
};

/* -- Sleep & delay -- */

extern void udelay(uint16_t us);
extern void mdelay(uint16_t ms);
extern void msleep(uint16_t ms);

/** Check for the presence of a file in FatFs */
extern int is_file_present(const char *path);

/* FatFS only supports 8.3 filenames, and we work from the current directory so
   paths should be short... */
#define MAX_PATH_LEN 64

extern int join_path(char *path, size_t n, const char *dir, const char *file);

struct pnm_header;
extern int open_image(const char *dir, const char *file, FILE *f,
		      struct pnm_header *hrd);

/* -- Debug utilities */

/** Print the contents of a buffer with offsets on stdout */
extern void dump_hex(const void *data, uint16_t len);
extern void dump_hex16(const void *data, uint16_t len);

void swap16(void *x);
void swap16_array(int16_t *x, uint16_t n);
void swap32(void *x);
void swap32_array(int32_t **x, uint16_t n);


/** reads a png file into a monochrome buffer */
int read_png(const char* file_name, png_byte ** image_ptr, int * width, int * heigth);

/** loads a file with register settings and supplies a vector containing all settings */
int read_register_settings_from_file(const char* filename, regSetting_t** settings);
void dump_register_settings(regSetting_t *settings, int n);

int maxstrcpy(char* to, char* from, size_t max);
int maxstr2memcpy(char* to, char* from, size_t maxMemSize);

unsigned long long read_stopwatch(struct timespec* starttime, char* label, int reset);
void start_stopwatch(struct timespec* starttime);
#endif /* INCLUDE_UTIL_H */