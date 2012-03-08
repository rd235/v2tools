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
#define DOTSIZE 16 * (1 << 20)
#define XSIZE (1 << 30)

unsigned inline long xordiff(unsigned long *b1, unsigned long *b2, unsigned long *b3,
		int bufsize)
{
	register int i;
	register unsigned long zero;
	for (i=zero=0; i<bufsize; i++) 
		zero |= (b3[i] = b1[i] ^ b2[i]);
	return zero;
}

unsigned inline long isnotzero(unsigned long *b, int bufsize)
{
	register int i;
	for (i=0; i<bufsize; i++)
		if (b[i])
			return 1;
	return 0;
}

void xorfile(struct ioent *f1, struct ioent *f2, struct ioent *fout, 
		struct ioent *fbiout, int blocksize, int flags)
{
	register off_t offset1;
	register off_t offset2;
	off_t nextdot=DOTSIZE;
	off_t nextX=XSIZE;
	register int bufsize=blocksize / sizeof(unsigned long);
	register int verbose=flags & XOR_VERBOSE;
	unsigned long *buf1,*buf2,*buf3,*buf4;
	int n1,n2;
	buf1 = malloc(blocksize);
	buf2 = malloc(blocksize);
	buf3 = malloc(blocksize);
	if (buf1 == NULL || buf2 == NULL || buf3 == NULL) {
		fprintf(stderr,"memory error");
		exit(1);
	}
	if (fbiout) {
		buf4 = malloc(blocksize);
		if (buf4 == NULL) {
			fprintf(stderr,"memory error");
			exit(1);
		}
		memset(buf4, 0, sizeof(long) * bufsize);
	}
	for (offset1=offset2=0, n2=blocksize; n2 >= blocksize; offset1 += n1, offset2 += n2) {
		n1=f1->ft->ft_read(f1,buf1,blocksize);
		n2=f2->ft->ft_read(f2,buf2,blocksize);
		if (__builtin_expect(n1 < blocksize, 0)) 
			memset(((char *)buf1)+n1, 0, blocksize-n1);
		if (__builtin_expect(n2 < blocksize, 0))
			memset(((char *)buf2)+n2, 0, blocksize-n2);
		//printf("off %lld %d %d %d\n",offset2,n1,n2,verbose);
		if (fbiout) {
			if (__builtin_expect(n1 > n2, 0)) {
				memcpy(((char *)buf4)+n2, ((char *)buf1)+n2, n1-n2);
				fbiout->ft->ft_write(fbiout,isnotzero(buf4,bufsize),buf4,n1,offset1);
			} else
				fbiout->ft->ft_write(fbiout,0,buf4,n1,offset1);
		} 
		fout->ft->ft_write(fout,xordiff(buf1,buf2,buf3,bufsize),buf3,n2,offset2);
		if (verbose) {
			while (offset2 >= nextdot) {
				nextdot+=DOTSIZE;
				if (offset2 >= nextX) {
					nextX += XSIZE;
					fprintf(stderr, "X\n");
				} else
					fprintf(stderr, ".");
			}
		}
	}
	fout->ft->ft_truncate(fout, offset2);
	//printf("%lld %lld\n",offset1,offset2);
	if (f1->hash || fbiout) {
		while ((n1=f1->ft->ft_read(f1,buf1,blocksize)) > 0) {
			if (fbiout) {
				if (__builtin_expect(n1 < blocksize,0)) 
					memset(((char *)buf1)+n1, 0, blocksize-n1);
				fbiout->ft->ft_write(fbiout,isnotzero(buf1,bufsize),buf1,n1,offset1);
				offset1 += n1;
				if (verbose) {
					while (offset2 >= nextdot) {
						nextdot+=DOTSIZE;
						if (offset2 >= nextX) {
							nextX += XSIZE;
							fprintf(stderr, "X\n");
						} else
							fprintf(stderr, ".");
					}
				}
			}
		}
		if (fbiout)
			fbiout->ft->ft_truncate(fbiout, offset1);
	}
	if (verbose)
		fprintf(stderr, "\n");
}

void usage(char *progname)
{
	fprintf(stderr,"Usage: %s [-v] [-s bufsize] {file1 | -} file2 filediff\n",progname);
	exit(1);

}

int main(int argc, char *argv[])
{
	static struct ioent f1, f2, fout, fbiout;
	static int flags;
	int blocksize=0;

	while (1) {
		int option_index = 0;
		int c;
		static struct option long_options[] = {
			{"help", no_argument, 0,  'h' },
			{"verbose", no_argument, 0,  'v' },
			{"bufsize", required_argument, 0,  's' },
			{0,         0,                 0,  0 }
		};

		c = getopt_long(argc, argv, "hvs:1234",
				long_options, &option_index);
		if (c == -1)
			break;

		switch(c) {
			case 's' : blocksize=atoi(optarg); break;
			case 'v': flags |= XOR_VERBOSE; break;
			case '1': f1.hash=mhash_init(MHASH_SHA1); break;
			case '2': f2.hash=mhash_init(MHASH_SHA1); break;
			case '3': fout.hash=mhash_init(MHASH_SHA1); break;
			case '4': fbiout.hash=mhash_init(MHASH_SHA1); break;
			case 'h': 
			default: usage(argv[0]);
		}
	}

	if (argc-optind < 3 || argc-optind > 4)
		usage(argv[0]);

	argc -= optind-1;
	argv += optind-1;

	open_ioent(&f1,argv[1],O_RDONLY,0);
	open_ioent(&f2,argv[2],O_RDONLY,0);
	open_ioent(&fout,argv[3],O_WRONLY|O_CREAT|O_EXCL,0666);
	if (blocksize == 0) {
		struct stat s;
		if (fout.ft != &ftfile || stat(argv[3],&s) < 0)
			blocksize = STDBLOCKSIZE;
		else
			blocksize = s.st_blksize;
	}
	if (argv[4]) {
		open_ioent(&fbiout,argv[4],O_WRONLY|O_CREAT|O_EXCL,0666);
		xorfile(&f1,&f2,&fout,&fbiout,blocksize,flags);
	} else
		xorfile(&f1,&f2,&fout,NULL,blocksize,flags);
	f1.ft->ft_close(&f1);
	f2.ft->ft_close(&f2);
	fout.ft->ft_close(&fout);
	if (argv[4])
		fbiout.ft->ft_close(&fbiout);
	if (f1.hash) printhash(f1.hash,"IN1",argv[1]);
	if (f2.hash) printhash(f2.hash,"IN2",argv[2]);
	if (fout.hash) printhash(fout.hash,"OUT",argv[3]);
	if (argv[4] && fbiout.hash) printhash(fbiout.hash,"ODX",argv[3]);
	return 0;
}
