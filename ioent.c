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

#define _GNU_SOURCE 
#define _FILE_OFFSET_BITS 64
#define O_LARGEFILE
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
#include <ioent.h>

#define STDBLOCKSIZE 4096
#define XOR_VERBOSE 0x1

ssize_t read_file(struct ioent *d, void *buf, size_t count)
{
	ssize_t rv=read(d->descr.fd, buf, count);
	if (d->hash)
		mhash(d->hash, buf, rv);
	return rv;
}

ssize_t read_bz2(struct ioent *d, void *buf, size_t count)
{
	ssize_t rv=BZ2_bzread(d->descr.bz, buf, count);
	if (d->hash && rv >= 0)
		mhash(d->hash, buf, rv);
	return rv;
}

ssize_t read_gz(struct ioent *d, void *buf, size_t count)
{
	ssize_t rv=gzread(d->descr.gz, buf, count);
	if (d->hash && rv >= 0)
		mhash(d->hash, buf, rv);
	return rv;
}

ssize_t write_file(struct ioent *d, long nonzero, void *buf, size_t count, off_t offset)
{
	ssize_t rv;
	if (nonzero)
		rv=pwrite(d->descr.fd, buf, count, offset);
	else
		rv=count;
	if (d->hash && rv >= 0)
		mhash(d->hash, buf, rv);
	return rv;
}

ssize_t write_stream(struct ioent *d, long nonzero, void *buf, size_t count, off_t offset)
{
	ssize_t rv=write(d->descr.fd, buf, count);
	if (d->hash && rv >= 0)
		mhash(d->hash, buf, rv);
	return rv;
}

ssize_t write_bz2(struct ioent *d, long nonzero, void *buf, size_t count, off_t offset)
{
	ssize_t rv=BZ2_bzwrite(d->descr.bz, buf, count);
	if (d->hash && rv >= 0)
		mhash(d->hash, buf, rv);
	return rv;
}

ssize_t write_gz(struct ioent *d, long nonzero, void *buf, size_t count, off_t offset)
{
	ssize_t rv=gzwrite(d->descr.gz, buf, count);
	if (d->hash && rv >= 0)
		mhash(d->hash, buf, rv);
	return rv;
}

int close_file(struct ioent *d)
{
	return close(d->descr.fd);
}

int close_bz2(struct ioent *d)
{
	BZ2_bzclose(d->descr.bz);
	return 0;
}

int close_gz(struct ioent *d)
{
	return gzclose(d->descr.gz);
}

int no_truncate(struct ioent *d, off_t len)
{
	return 0;
}

int truncate_file(struct ioent *d, off_t len)
{
	return ftruncate(d->descr.fd, len);
}

struct filetype ftfile={read_file, write_file, truncate_file, close_file};
struct filetype ftstream={read_file, write_stream, no_truncate, close_file};
struct filetype ftbz2={read_bz2, write_bz2, no_truncate, close_bz2};
struct filetype ftgz={read_gz, write_gz, no_truncate, close_gz};

static char hex[]="0123456789abcdef";
void printhash(MHASH td, char *name, char *arg)
{
	unsigned char out[mhash_get_block_size(MHASH_SHA1)];
	char outstr[mhash_get_block_size(MHASH_SHA1)*2+1];
	int i;
	mhash_deinit(td, out);
	for (i=0; i<mhash_get_block_size(MHASH_SHA1); i++) {
		outstr[2*i]=hex[out[i] >> 4];
		outstr[2*i+1]=hex[out[i] & 0xf];
	}
	outstr[2*i]=0;
	fprintf(stderr,"%s %s %s\n",outstr,name,arg);
}

static char *flag2mode[]={"r","w","rw"};
static int flag2std[]={STDIN_FILENO,STDOUT_FILENO,STDOUT_FILENO};
void open_ioent(struct ioent *fx, char *filename, int flags, int mode)
{
	if (strlen(filename) > 4 && strcmp(".bz2",filename+(strlen(filename)-4))==0) {
		fx->ft = &ftbz2;
		if (strlen(filename) == 5 && *filename == '-')
			fx->descr.bz = BZ2_bzdopen(flag2std[flags&O_ACCMODE],flag2mode[flags&O_ACCMODE]);
		else
			fx->descr.bz = BZ2_bzopen(filename,flag2mode[flags&O_ACCMODE]);
		if (fx->descr.bz == NULL) {
			perror(filename);
			exit(1);
		}
	} else if (strlen(filename) > 3 && strcmp(".gz",filename+(strlen(filename)-3))==0) {
		fx->ft = &ftgz;
		if (strlen(filename) == 4 && *filename == '-')
			fx->descr.bz = gzdopen(flag2std[flags&O_ACCMODE],flag2mode[flags&O_ACCMODE]);
		else
			fx->descr.gz = gzopen(filename,flag2mode[flags&O_ACCMODE]);
		if (fx->descr.gz == NULL) {
			perror(filename);
			exit(1);
		}
	} else {
		if (strcmp(filename,"-")==0) {
			struct stat st;
			if (fstat(flag2std[flags&O_ACCMODE],&st)==0 && S_ISREG(st.st_mode))
				fx->ft = &ftfile;
			else
				fx->ft = &ftstream;
			fx->descr.fd=flag2std[flags&O_ACCMODE];
		} else {
			fx->ft = &ftfile;
			fx->descr.fd=open(filename,flags,mode);
		}
		if (fx->descr.fd < 0) {
			perror(filename);
			exit(1);
		}
	}
}
