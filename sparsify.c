/*   
 *   Sparsify: deallocate unused blocks (zero blocks) in a file
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
#include <config.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <getopt.h>
#include <bzlib.h>
#include <zlib.h>
#include <ioent.h>

#if HAVE_FALLOCATE == 1
#include <linux/falloc.h>
#else
#define FALLOC_FL_KEEP_SIZE 0x01
#define fallocate(A,B,C,D) (ENOENT)
#warning OLD kernel headers, fallocate disabled
#endif

#define STDBLOCKSIZE 4096
#define SPARSIFY_VERBOSE 0x1
#define SPARSIFY_DELETE 0x2
#define SPARSIFY_COPY 0x4
#define SPARSIFY_FORCE1 0x10
#define SPARSIFY_FORCE2 0x20
#define SPARSIFY_FORCE3 0x40
#define SPARSIFY_HASH1 0x100
#define SPARSIFY_HASH2 0x200
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE 0x02
#endif

inline void verboseprint(off_t offset)
{
	if ((offset & 0xffffff)== 0) {
		if ((offset & 0x3fffffff)== 0x3f000000)
			fprintf(stderr, "X\n");
		else
			fprintf(stderr, ".");
	}
}

inline unsigned long iszero(unsigned long *b, int bufsize)
{
	register int i;
	for (i=0; i<bufsize; i++)
		if (b[i])
			return 0;
	return 1;
}

void dangerous_sparsify(int fd, int fdout, int filesize, int blocksize, int verbose)
{
	register off_t offset;
	register int bufsize=blocksize / sizeof(unsigned long);
	unsigned long buf[bufsize];
	ssize_t n;
	for (offset=((filesize + blocksize - 1) / blocksize) * blocksize; 
			offset >= 0; offset -= blocksize) {
		n=pread(fd,buf,blocksize,offset);
		//printf("READ %lld %d\n",offset,n);
		if (__builtin_expect(n<blocksize,0))
			memset(((char *)buf)+n, 0, blocksize-n);
		if (!iszero(buf,bufsize) && n > 0) {
			//printf("WRITE %lld %d\n",offset,n);
			pwrite(fdout,buf,n,offset);
		}
		ftruncate(fd,offset);
		if (verbose) verboseprint(offset);
	}
	if (verbose) fprintf(stderr, "\n");
	close(fd);
	close(fdout);
}

void real_sparsify(int fd, int blocksize, int verbose)
{
	register off_t offset;
	register int bufsize=blocksize / sizeof(unsigned long);
	unsigned long buf[bufsize];
	ssize_t n;
	for (offset=0, n=blocksize; n >= blocksize; offset += n) {
		n=read(fd,buf,blocksize);
		if (__builtin_expect(n<blocksize,0))
			memset(((char *)buf)+n, 0, blocksize-n);
		if (iszero(buf,bufsize) && n > 0) {
			int rv=fallocate(fd,FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,offset,n);
			if (rv < 0) {
				perror("fallocate");
				break;
			}
		}
		if (verbose) verboseprint(offset);
	}
	if (verbose) fprintf(stderr, "\n");
	close(fd);
}

void copy_sparsify(struct ioent *fin, struct ioent *fout, int blocksize, int verbose)
{
	register off_t offset;
	register int bufsize=blocksize / sizeof(unsigned long);
	unsigned long buf[bufsize];
	ssize_t n;
	for (offset=0,n=blocksize; n>=blocksize; offset+=n) {
		n=fin->ft->ft_read(fin,buf,blocksize);
		if (__builtin_expect(n<blocksize,0))
			memset(((char *)buf)+n, 0, blocksize-n);
		fout->ft->ft_write(fout,!iszero(buf,bufsize) && n>0, buf, n, offset);
		if (verbose) verboseprint(offset);
	}
	if (verbose) fprintf(stderr, "\n");
	fout->ft->ft_truncate(fout, offset);
	fin->ft->ft_close(fin);
	fout->ft->ft_close(fout);
}

void usage(char *progname)
{
  fprintf(stderr,"Usage: %s [-d] file1 file2\n"
			           "       %s [-fff][-c] file\n",progname,progname);
	exit(1);
}

int main(int argc, char *argv[])
{
	int blocksize=0;
	static int flags;
	int fd;

	while (1) {
		int option_index = 0;
		int c;
		static struct option long_options[] = {
			{"help", no_argument, 0,  'h' },
			{"force", no_argument, 0,  'f' },
			{"verbose", no_argument, 0,  'v' },
			{"bufsize", required_argument, 0,  's' },
			{"delete", required_argument, 0,  'd' },
			{"copy", required_argument, 0,  'c' },
			{0,         0,                 0,  0 }
		};
		c = getopt_long(argc, argv, "hfdscv12",
				long_options, &option_index);
		if (c == -1)
			break;

		switch(c) {
			case 's' : blocksize=atoi(optarg); break;
			case 'v': flags |= SPARSIFY_VERBOSE; break;
			case 'd': flags |= SPARSIFY_DELETE; break;
			case 'c': flags |= SPARSIFY_COPY; break;
			case '1': flags |= SPARSIFY_HASH1; break;
			case '2': flags |= SPARSIFY_HASH2; break;
			case 'f': if (flags & SPARSIFY_FORCE2)
									flags |= SPARSIFY_FORCE3;
								else if (flags & SPARSIFY_FORCE1)
									flags |= SPARSIFY_FORCE2;
								else
									flags |= SPARSIFY_FORCE1;
								break;
			case 'h':
			default: usage(argv[0]);
		}
	}

	if (argc-optind < 1 || argc-optind > 2)
		usage(argv[0]);
	/* renumbering of the arguments */
	argv[optind-1]=argv[0];
	argc -= optind-1;
	argv += optind-1;
	if ((flags & SPARSIFY_FORCE1) && argc==3) {
		fprintf(stderr,"-f option is not for copy mode\n");
		exit(1);
	}
	if ((flags & SPARSIFY_DELETE) && argc==2) {
		fprintf(stderr,"-d option is for copy mode\n");
		exit(1);
	}
	if ((flags & SPARSIFY_FORCE1) && (flags & SPARSIFY_COPY)) {
		fprintf(stderr,"-f and -c options are mutually exclusive\n");
		exit(1);
	}
	if ((flags & SPARSIFY_FORCE1) && !(flags & SPARSIFY_FORCE3)) {
		fprintf(stderr,"-f option is very dangerous\n"
				"incomplete executions result in non recoverable corruption of data\n"
				"-f must be set three times -fff\n");
		exit(1);
	}

	if (argc==3) {
		/* copy mode */
		struct stat st;
		static struct ioent fin, fout;
		int mode;
		if (stat(argv[1],&st) >= 0)
			mode = st.st_mode&0777;
		else
			mode = 0666;
		open_ioent(&fout,argv[2],O_WRONLY|O_TRUNC|O_CREAT|O_EXCL,mode);
		if (blocksize == 0) {
			if (fout.ft != &ftfile || stat(argv[3],&st) < 0)
				blocksize = STDBLOCKSIZE;
			else
				blocksize = st.st_blksize;
		}
		open_ioent(&fin,argv[1],O_RDONLY,0);
		if (flags & SPARSIFY_HASH1) fin.hash=mhash_init(MHASH_SHA1);
		if (flags & SPARSIFY_HASH2) fout.hash=mhash_init(MHASH_SHA1);
		copy_sparsify(&fin,&fout,blocksize,flags & SPARSIFY_VERBOSE);
		if (flags & SPARSIFY_DELETE)
			unlink(argv[1]);
		if (fin.hash) printhash(fin.hash,"IN ",argv[1]);
		if (fout.hash) printhash(fout.hash,"OUT",argv[2]);
	} else {
		/* on the same file */
		struct stat st;
		fd=open(argv[1],O_RDWR);
		if (fd < 0) {
			perror(argv[1]);
			exit(1);
		}
		fstat(fd,&st); 
		if (!S_ISREG(st.st_mode)) {
			fprintf(stderr,"%s is not a regular file\n",argv[1]);
			exit(1);
		}
		if (blocksize == 0)
			blocksize = st.st_blksize;
		if ((flags & SPARSIFY_COPY) || (flags & SPARSIFY_FORCE3)) {
			char *tmpfile;
			asprintf(&tmpfile,"%s/.%s.sp%d",dirname(argv[1]),basename(argv[1]),getpid());
			int fdout=open(tmpfile,O_WRONLY|O_TRUNC|O_CREAT|O_EXCL,st.st_mode&0777);
			if (fdout < 0) {
				perror(tmpfile);
				exit(1);
			}
			if (flags & SPARSIFY_COPY) {
				struct ioent fin={.ft=&ftfile, .descr.fd=fd, .hash=NULL};
				struct ioent fout={.ft=&ftfile, .descr.fd=fdout, .hash=NULL};
				copy_sparsify(&fin,&fout,blocksize,flags & SPARSIFY_VERBOSE);
			} else if (flags & SPARSIFY_FORCE3)
				dangerous_sparsify(fd,fdout,st.st_size,blocksize,flags & SPARSIFY_VERBOSE);
			else
				exit(1);
			rename(tmpfile,argv[1]);
		} else {
			real_sparsify(fd,blocksize,flags & SPARSIFY_VERBOSE);
		}
	}
	return 0;
}
