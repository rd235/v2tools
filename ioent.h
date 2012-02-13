/*   
 *   Xordiff: Create a diff between two versions of the same file and store it
 *   as a sparse file
 *
 *   Copyright 2012 Renzo Davoli
 *   
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License, version 3 or (at your option) 
 *   any later version, as published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 */

#ifndef IOENT_H
#define IOENT_H

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <mhash.h>
#include <bzlib.h>
#include <zlib.h>

#define STDBLOCKSIZE 4096
#define XOR_VERBOSE 0x1

struct ioent;

struct filetype {
	ssize_t (*ft_read)(struct ioent *d, void *buf, size_t count);
	ssize_t (*ft_write)(struct ioent *d, long nonzero, void *buf, size_t count, off_t offset);
	int (*ft_truncate)(struct ioent *d, off_t len);
	int (*ft_close)(struct ioent *d);
};

struct ioent {
	struct filetype *ft;
	union {
		int fd;
		BZFILE *bz;
		gzFile gz;
	} descr;
	MHASH hash;
};

struct filetype ftfile;
struct filetype ftstream;
struct filetype ftbz2;
struct filetype ftgz;

void printhash(MHASH td, char *name, char *arg);
void open_ioent(struct ioent *fx, char *filename, int flags, int mode);
#endif
