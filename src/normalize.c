/*
	normalize.c - main source file for PCM WAV normalizer - v0.253
	(c) 2000-2004 Manuel Kasper <mk@neon1.net>
	smartpeak code by Lapo Luchini <lapo@lapo.it>.

	This file is part of normalize.

	normalize is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	
	normalize is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "pcmwav.h"

#define COPYRIGHT_NOTICE	"normalize v0.253 (c) 2000-2004 Manuel Kasper <mk@neon1.net>.\n" \
							"All rights reserved.\n" \
							"smartpeak code by Lapo Luchini <lapo@lapo.it>."

void			*buf;
unsigned char	*buf8;
unsigned short	*buf16;
signed char		*table8;
signed short	*table16;
unsigned long	iobufsize = 65536;
pcmwavfile		pwf;
HANDLE			outf;
double			ratio, normpercent = 100.0, peakpercent = 100.0;
int				smartpeak = 0;
double			mingain = 0;
int				usemingain = 0;
int				quiet = 0, nooverwrite = 0;
char			outfname[1024];
int				dowhat = 0, prompt = 0;
int				dontabort = 0;

void make_table8(void);
void make_table16(void);
int getpeaks8(signed char *minpeak, signed char *maxpeak);
int getpeaks16(signed short *minpeak, signed short *maxpeak);
unsigned long amplify8(void);
unsigned long amplify16(void);
unsigned long passthrough(void);
int process_filespec(char *fspec);
int process_file(char *fname);
void usage(void);

int main(int argc, char *argv[]) {

	int				i;
	
	/* Parse command line */
	for (i = 1; i < argc; i++) {
		if ((argv[i][0] == '-') && (argv[i][1] != 0x00)) {
			switch (argv[i][1]) {
				case 'h':
					usage();
					return 0;
				case 'q':
					quiet = 1;
					break;
				case 'd':
					dontabort = 1;
					break;
				case 'p':
					prompt = 1;
					break;
				case 'm':
					normpercent = atof(argv[++i]);
					break;
				case 's':
					peakpercent = atof(argv[++i]);
					if (peakpercent < 50.0)
						peakpercent = 50.0;
					if (peakpercent < 100.0)
						smartpeak = 1;
					break;
				case 'o':
					nooverwrite = 1;
					strcpy(outfname, argv[++i]);
					break;
				case 'x':
					mingain = atof(argv[++i]);
					usemingain = 1;
					break;
				case 'l':
					if (dowhat != 0) {
						fprintf(stderr, "You can't specify both -l and -a. Aborting.\n");
						return 2;
					} else {
						dowhat = 1;
						ratio = atof(argv[++i]);
					}
					break;
				case 'a':
					if (dowhat != 0) {
						fprintf(stderr, "You can't specify both -l and -a. Aborting.\n");
						return 2;
					} else {
						dowhat = 2;
						ratio = pow(10, atof(argv[++i]) / 20);
					}
					break;
				case 'b':
					iobufsize = atoi(argv[++i]) * 1024;
					if ((iobufsize < 16384) || (iobufsize > 16777216)) {
						fprintf(stderr, "I/O buffer size must be between 16 and 16384 KB.\n");
						return 2;
					}
					break;
				default:
					fprintf(stderr, "Error: Can't understand flag -%c. Aborting.\n", argv[i][1]);
					return 2;
				}
		} else {
			break;
		}
	}

	// this way the percentile peak is amplified to the correct level
	if (smartpeak)
		normpercent *= peakpercent / 100.0;

	if (i >= argc) {
		usage();
		return 2;
	}

	if (!quiet)
		fprintf(stderr, "\n%s\n\n", COPYRIGHT_NOTICE);

	return process_filespec(argv[i]);

	return 0;
}

int process_filespec(char *fspec) {
	long	hFile;
	char	myfullpath[_MAX_PATH];
	char	drive[_MAX_DRIVE];
	char	dir[_MAX_DIR];
	struct	_finddata_t my_file;
	int		err;

	_fullpath(myfullpath, fspec, _MAX_PATH);
	_splitpath(myfullpath, drive, dir, NULL, NULL);

	if ((hFile = _findfirst(fspec, &my_file)) == -1L)
		fprintf(stderr, "Could not find file %s.\n", fspec);
	else {
		
		do {

			if (my_file.attrib & _A_SUBDIR)
				continue;

			sprintf(myfullpath, "%s%s%s", drive, dir, my_file.name);
			
			err = process_file(myfullpath);
			if (err && (err != 3)) {
				
				if ((err != 5) || !dontabort)
					return err;
			}

		} while (_findnext(hFile, &my_file) == 0);

		_findclose(hFile);
	}

	return err;
}

int process_file(char *fname) {

	clock_t		sclk, eclk;
	double		atime;
	unsigned long	ndata;

	if (!quiet) {
		
		fprintf(stderr, "-------------------------------------------------------------------------------\n");
		fprintf(stderr, "Processing file %s\n\n", fname);
	}

	// Open PCM WAV file
	if (!pcmwav_open(fname, GENERIC_READ | GENERIC_WRITE, &pwf)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 1;
	}

	if (nooverwrite) {
		char	hdrbuf[16384];
		DWORD	nread;

		outf = CreateFile(outfname, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (outf == INVALID_HANDLE_VALUE) {
			if (!quiet)
				fprintf(stderr, "Couldn't open output file '%s'.\n", outfname);
			return 1;
		}
		// Copy headers
		SetFilePointer(pwf.winfile, 0, NULL, FILE_BEGIN);
		ReadFile(pwf.winfile, hdrbuf, pwf.datapos, &nread, NULL);
		if (nread != pwf.datapos) {
			if (!quiet)
				fprintf(stderr, "Could not copy headers.\n");
			return 1;
		}
		SetFilePointer(pwf.winfile, pwf.datapos, NULL, FILE_BEGIN);
		WriteFile(outf, hdrbuf, pwf.datapos, &nread, NULL);
		if (nread != pwf.datapos) {
			if (!quiet)
				fprintf(stderr, "Could not copy headers.\n");
			return 1;
		}
	}

	// Allocate buffer
	buf = VirtualAlloc(NULL, iobufsize, MEM_COMMIT, PAGE_READWRITE);

	if (buf == NULL) {
		if (!quiet)
			fprintf(stderr, "Cannot allocate buffer in memory.\n");
		return 1;
	}

	buf8 = (signed char*)buf;
	buf16 = (signed short*)buf;

	if (dowhat == 0) {
		if (pwf.bitspersample == 8) {
			signed char	mins, maxs;

			if (!quiet)
				fprintf(stderr, "Pass 1: Finding peak levels...\n");

			if (!getpeaks8(&mins, &maxs))
				return 1;

			if (!quiet)
				fprintf(stderr, "\rMinimum level found: %d, maximum level found: %d\n", (short)mins, (short)maxs);

			if (mins == -128)
				mins = -127;

			if ((-mins) > maxs)
				maxs = -mins;

			if (maxs == 0) {
				if (!quiet)
					fprintf(stderr, "All zero samples found.\n");
				ratio = 1;
			} else {
				ratio = (127.0 * normpercent) / ((double)maxs * 100.0);
			}

		} else if (pwf.bitspersample == 16) {
			signed short	mins, maxs;

			if (!quiet)
				fprintf(stderr, "Pass 1: Finding peak levels...\n");

			if (!getpeaks16(&mins, &maxs))
				return 1;

			if (!quiet)
				fprintf(stderr, "\rMinimum level found: %d, maximum level found: %d\n", mins, maxs);

			if (mins == -32768)
				mins = -32767;

			if ((-mins) > maxs)
				maxs = -mins;

			if (maxs == 0) {
				if (!quiet)
					fprintf(stderr, "All zero samples found.\n");
				ratio = 1;
			} else {
				ratio = (32767.0 * normpercent) / ((double)maxs * 100.0);
			}
		}
	}

	if (ratio == 1) {
		if (!quiet)
			fprintf(stderr, "No amplification required; skipping.\n");
		if (nooverwrite) {
			/* copy existing data */
			ndata = passthrough();
			VirtualFree(buf, 0, MEM_RELEASE);
			pcmwav_close(&pwf);
			CloseHandle(outf);
		}
		return 3;
	} else if (ratio < 1) {
		if (!quiet)
			fprintf(stderr, "Performing attenuation of %.03f dB\n", 20.0 * log10(ratio));
	} else if (ratio > 1) {
		if (!quiet)
			fprintf(stderr, "Performing amplification of %.03f dB\n", 20.0 * log10(ratio));
	}

	if (usemingain) {
		if (fabs(20.0 * log10(ratio)) < mingain) {
			if (!quiet)
				fprintf(stderr, "Level is smaller than %.03f dB, aborting.\n", mingain);
			if (nooverwrite) {
				/* copy existing data */
				ndata = passthrough();
				VirtualFree(buf, 0, MEM_RELEASE);
				pcmwav_close(&pwf);
				CloseHandle(outf);
			}
			return 3;
		}
	}

	if (prompt) {
		char	inanswer;
		fflush(stdin);
		fprintf(stderr, "\nStart normalization? (Y/N) ");
		inanswer = getchar();
		if ((inanswer != 'y') && (inanswer != 'Y')) {
			VirtualFree(buf, 0, MEM_RELEASE);
			pcmwav_close(&pwf);
			if (nooverwrite)
				CloseHandle(outf);
			return 5;
		}
	}

	if (!quiet)
		fprintf(stderr, "\nAmplifying...\n");

	sclk = clock();
	if (pwf.bitspersample == 8) {
		table8 = (signed char*)VirtualAlloc(NULL, 256, MEM_COMMIT, PAGE_READWRITE);

		if (table8 == NULL) {
			if (!quiet)
				fprintf(stderr, "Cannot allocate translation table in memory.\n");
			return 4;
		}

		make_table8();
		ndata = amplify8();
		VirtualFree(table8, 0, MEM_RELEASE);

	} else if (pwf.bitspersample == 16) {
		table16 = (signed short*)VirtualAlloc(NULL, 131072, MEM_COMMIT, PAGE_READWRITE);

		if (table16 == NULL) {
			if (!quiet)
				fprintf(stderr, "Cannot allocate translation table in memory.\n");
			return 4;
		}
		make_table16();
		ndata = amplify16();
		VirtualFree(table16, 0, MEM_RELEASE);
	}
	eclk = clock();

	if (!quiet)
		fprintf(stderr, "\n\nDone.\n");

	atime = (double)(eclk - sclk) / (double)CLOCKS_PER_SEC;

	if (atime < 1.0) {
		if (!quiet)
			fprintf(stderr, "Time taken: %.01f sec.\n", atime);
	} else {
		if (!quiet)
			fprintf(stderr, "Time taken: %.01f sec. (throughput: %.03f MBps)\n",
				atime, ((double)ndata / 1048576.0) / atime);
	}

	VirtualFree(buf, 0, MEM_RELEASE);
	pcmwav_close(&pwf);

	if (nooverwrite)
		CloseHandle(outf);

	return 0;
}

#pragma optimize("", off)
void make_table8(void) {
	unsigned char	i = 0;

	do {
		if (((signed char)i * ratio) > 127.0)
			table8[i ^ 0x80] = (signed char)0xFF;
		else if (((signed char)i * ratio) < -127.0)
			table8[i ^ 0x80] = 0x00;
		else
			table8[i ^ 0x80] = (signed char)(((signed char)i) * ratio) ^ 0x80;
	} while (++i);
}
#pragma optimize("", on)

#pragma optimize("", off)
void make_table16(void) {
	unsigned short	i = 0;

	do {
		if (((signed short)i * ratio) > 32767)
			table16[i] = 32767;
		else if (((signed short)i * ratio) < -32767)
			table16[i] = -32767;
		else
			table16[i] = (signed short)(((signed short)i) * ratio);
	} while (++i);
}
#pragma optimize("", on)

int getpeaks8(signed char *minpeak, signed char *maxpeak) {
	unsigned long				i, ndone = 0, readn;
	register signed char		minp = 0, maxp = 0, cur;
	int							npercent, lastn = -1;
	unsigned long				*stats;
	unsigned long				numstat;

	if (smartpeak) {
		// allocate memory for the sample statistics
		stats = (unsigned long*)VirtualAlloc(NULL, sizeof(unsigned long) * 256, MEM_COMMIT, PAGE_READWRITE);
		
		if (stats == NULL) {
			if (!quiet)
				fprintf(stderr, "Cannot allocate buffer in memory.\n");
			return 0;
		}

		for (i = 0; i < 256; i++)
			stats[i] = 0;

		numstat = 0;
	}

	
	readn = iobufsize;
	if (pwf.ndatabytes < iobufsize)
		readn = pwf.ndatabytes;

	if (!pcmwav_read(&pwf, buf, readn)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 0;
	}

	while (readn) {
		for (i = 0; i < readn; i++) {
			cur = buf8[i] ^ 0x80;

			if (smartpeak) {
				stats[128 + cur]++;
				numstat++;
			} else {
				if (cur < minp)
					minp = cur;
				if (cur > maxp)
					maxp = cur;	
			}
		}

		ndone += readn;

		if (!quiet) {
			npercent = (int)(100.0 * ((double)ndone / (double)pwf.ndatabytes));
			if (npercent > lastn) {
				fprintf(stderr, "\r%d%%", npercent);
				fflush(stderr);
				lastn = npercent;
			}
		}

		readn = iobufsize;
		if (readn > (pwf.ndatabytes - ndone))
			readn = pwf.ndatabytes - ndone;

		if (readn) {
			if (!pcmwav_read(&pwf, buf, readn)) {
				if (!quiet)
					fprintf(stderr, "%s\n", pcmwav_error);
				return 0;
			}
		}
		else
			break;
	}

	if (smartpeak) {
		// let's find how many samples is <percent> of the max
		numstat *= 1.0 - (peakpercent / 100.0);
		// let's use this to accumulate values
		ndone = 0;
		// let's count the min sample value that has the given percentile
		for (i = 0; (i < 256) && (ndone <= numstat); i++)
			ndone += stats[i];
		minp = i - 129;
		// let's count the max sample value that has the given percentile
		ndone = 0;
		for (i = 255; (i >= 0) && (ndone <= numstat); i--)
			ndone += stats[i];
		maxp = i - 127;
		VirtualFree(stats, 0, MEM_RELEASE);
	}

	pcmwav_rewind(&pwf);

	*minpeak = minp;
	*maxpeak = maxp;

	return 1;
}

int getpeaks16(signed short *minpeak, signed short *maxpeak) {
	unsigned long				i, ndone = 0, readn;
	register signed short		minp = 0, maxp = 0, cur;
	int							npercent, lastn = -1;
	unsigned long				*stats;
	unsigned long				numstat;

	if (smartpeak) {
		// allocate memory for the sample statistics
		stats = (unsigned long*)VirtualAlloc(NULL, sizeof(unsigned long) * 65536, MEM_COMMIT, PAGE_READWRITE);
		
		if (stats == NULL) {
			if (!quiet)
				fprintf(stderr, "Cannot allocate buffer in memory.\n");
			return 0;
		}

		for (i = 0; i < 65536; i++)
			stats[i] = 0;

		numstat = 0;
	}

	
	readn = iobufsize;
	if (pwf.ndatabytes < iobufsize)
		readn = pwf.ndatabytes;

	if (!pcmwav_read(&pwf, buf, readn)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 0;
	}

	while (readn) {
		for (i = 0; i < (readn>>1); i++) {
			cur = buf16[i];
			if (smartpeak) {
				stats[32768 + cur]++;
				numstat++;
			} else {
				if (cur < minp)
					minp = cur;
				if (cur > maxp)
					maxp = cur;
			}
		}

		ndone += readn;

		if (!quiet) {
			npercent = (int)(100.0 * ((double)ndone / (double)pwf.ndatabytes));
			if (npercent > lastn) {
				fprintf(stderr, "\r%d%%", npercent);
				fflush(stderr);
				lastn = npercent;
			}
		}

		readn = iobufsize;
		if (readn > (pwf.ndatabytes - ndone))
			readn = pwf.ndatabytes - ndone;

		if (readn) {
			if (!pcmwav_read(&pwf, buf, readn)) {
				if (!quiet)
					fprintf(stderr, "%s\n", pcmwav_error);
				return 0;
			}
		}
		else
			break;
	}

	if (smartpeak) {
		// let's find how many samples is <percent> of the max
		numstat *= 1.0 - (peakpercent / 100.0);
		// let's use this to accumulate values
		ndone = 0;
		// let's count the min sample value that has the given percentile
		for (i = 0; (i < 65536) && (ndone <= numstat); i++)
			ndone += stats[i];
		minp = i - 32769;
		// let's count the max sample value that has the given percentile
		ndone = 0;
		for (i = 65535; (i >= 0) && (ndone <= numstat); i--)
			ndone += stats[i];
		maxp = i - 32767;
		VirtualFree(stats, 0, MEM_RELEASE);
	}

	pcmwav_rewind(&pwf);

	*minpeak = minp;
	*maxpeak = maxp;

	return 1;
}

unsigned long amplify8(void) {
	unsigned long	i, ndone = 0, readn;
	long			nwr;
	int				npercent, lastn = -1;
	
	readn = iobufsize;
	if (pwf.ndatabytes < iobufsize)
		readn = pwf.ndatabytes;

	if (!pcmwav_read(&pwf, buf, readn)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 0;
	}

	while (readn) {
		for (i = 0; i < readn; i++) {
			buf8[i] = table8[(unsigned char)buf8[i]];
		}

		if (!nooverwrite) {
			nwr = -((long)readn);
			pcmwav_seek(&pwf, nwr);

			if (!pcmwav_write(&pwf, buf, readn)) {
				if (!quiet)
					fprintf(stderr, "%s\n", pcmwav_error);
				return 0;
			}
		} else {
			DWORD	nwritten;
			WriteFile(outf, buf, readn, &nwritten, NULL);
			if (nwritten != readn) {
				if (!quiet)
					fprintf(stderr, "Output file write error.\n");
				return 0;
			}
		}

		ndone += readn;

		if (!quiet) {
			npercent = (int)(100.0 * ((double)ndone / (double)pwf.ndatabytes));
			if (npercent > lastn) {
				fprintf(stderr, "\r%d%%", npercent);
				fflush(stderr);
				lastn = npercent;
			}
		}

		readn = iobufsize;
		if (readn > (pwf.ndatabytes - ndone))
			readn = pwf.ndatabytes - ndone;

		if (readn) {
			if (!pcmwav_read(&pwf, buf, readn)) {
				if (!quiet)
					fprintf(stderr, "%s\n", pcmwav_error);
				return 0;
			}
		}
		else
			break;
	}

	return ndone;
}

unsigned long amplify16(void) {
	unsigned long	i, ndone = 0, readn;
	long			nwr;
	int				npercent, lastn = -1;
	
	readn = iobufsize;
	if (pwf.ndatabytes < iobufsize)
		readn = pwf.ndatabytes;

	if (!pcmwav_read(&pwf, buf, readn)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 0;
	}

	while (readn) {
		for (i = 0; i < (readn>>1); i++) {
			buf16[i] = table16[buf16[i]];
		}

		if (!nooverwrite) {
			nwr = -((long)readn);
			pcmwav_seek(&pwf, nwr);
			
			if (!pcmwav_write(&pwf, buf, readn)) {
				if (!quiet)
					fprintf(stderr, "%s\n", pcmwav_error);
				return 0;
			}
		} else {
			DWORD nwritten;
			WriteFile(outf, buf, readn, &nwritten, NULL);
			if (nwritten != readn) {
				if (!quiet)
					fprintf(stderr, "Output file write error.\n");
				return 0;
			}
		}

		ndone += readn;

		if (!quiet) {
			npercent = (int)(100.0 * ((double)ndone / (double)pwf.ndatabytes));
			if (npercent > lastn) {
				fprintf(stderr, "\r%d%%", npercent);
				fflush(stderr);
				lastn = npercent;
			}
		}

		readn = iobufsize;
		if (readn > (pwf.ndatabytes - ndone))
			readn = pwf.ndatabytes - ndone;

		if (readn) {
			if (!pcmwav_read(&pwf, buf, readn)) {
				if (!quiet)
					fprintf(stderr, "%s\n", pcmwav_error);
				return 0;
			}
		}
		else
			break;
	}

	return ndone;
}

unsigned long passthrough(void) {
	unsigned long	ndone = 0, readn;
	int				npercent, lastn = -1;
	
	readn = iobufsize;
	if (pwf.ndatabytes < iobufsize)
		readn = pwf.ndatabytes;

	if (!pcmwav_read(&pwf, buf, readn)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 0;
	}

	while (readn) {
		DWORD nwritten;
		WriteFile(outf, buf, readn, &nwritten, NULL);
		if (nwritten != readn) {
			if (!quiet)
				fprintf(stderr, "Output file write error.\n");
			return 0;
		}

		ndone += readn;

		if (!quiet) {
			npercent = (int)(100.0 * ((double)ndone / (double)pwf.ndatabytes));
			if (npercent > lastn) {
				fprintf(stderr, "\r%d%%", npercent);
				fflush(stderr);
				lastn = npercent;
			}
		}

		readn = iobufsize;
		if (readn > (pwf.ndatabytes - ndone))
			readn = pwf.ndatabytes - ndone;

		if (readn) {
			if (!pcmwav_read(&pwf, buf, readn)) {
				if (!quiet)
					fprintf(stderr, "%s\n", pcmwav_error);
				return 0;
			}
		}
		else
			break;
	}

	return ndone;
}

void usage(void) {
	fprintf(stderr, "\n%s\nVisit http://neon1.net/ for updates.\n\n", COPYRIGHT_NOTICE);	
	fprintf(stderr,
		"    Usage:  normalize [flags] input-file\n\n"

		"        -l <ratio>   don't find peaks but multiply each sample by <ratio>\n"
		"        -a <level>   don't find peaks; amplify by <level> (given in dB)\n"
		"        -m <percent> normalize to <percent> % (default 100)\n"
		"        -s <percent> smartpeak: count as a peak only a signal that has the\n"
		"                     given percentile (50%%-100%%)\n"
		"        -x <level>   abort if gain increase is smaller than <level> (in dB)\n"
		"        -p           prompt before starting normalization\n"
		"        -b <size>    specify I/O buffer size (in KB; 16..16384; default 64)\n"
		"        -o <file>    write output to <file> (instead of overwriting original)\n"
		"        -q           quiet (no screen output)\n"
		"        -d           don't abort batch if user skips normalization of one file\n"
		"        -h           display this help\n\n"

		"    error levels: 0 = no error, 1 = I/O error, 2 = parameter error,\n"
		"                  3 = no amplification required, 4 = out of memory,\n"
		"                  5 = user abort\n\n"
		
		"	- wildcards are allowed in 'input-file' (e.g. normalize *.wav)\n"
		"	- 'input-file' needs to be a PCM WAV file.\n");
}
