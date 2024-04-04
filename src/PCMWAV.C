/*
	pcmwav.c - source file for PCM WAV I/O - v0.251
	(c) 2000-2004 Manuel Kasper <mk@neon1.net>

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
#include "pcmwav.h"
#include <stdio.h>
#include <windows.h>

char pcmwav_error[256];

int pcmwav_open(char *fname, DWORD access, pcmwavfile *opwf) {

	RIFFhdr		rhdr;
	fmt_sub		fmt;
	DWORD		nread;
	char		have_fmt = 0;
	unsigned long	subchunk, subchunk_size;

	opwf->winfile = CreateFile(fname, access, 0, NULL,
						OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (opwf->winfile == INVALID_HANDLE_VALUE) {
		sprintf(pcmwav_error, "Cannot open file %s.\n", fname);
		return 0;
	}

	// Read RIFF header
	ReadFile(opwf->winfile, &rhdr, sizeof(rhdr), &nread, NULL);

	// Check it
	if ((rhdr.ChunkID != 0x46464952 /* 'RIFF' */) || (rhdr.Format != 0x45564157 /* 'WAVE' */)) {
		sprintf(pcmwav_error, "This is not a PCM WAV file.\n");
		CloseHandle(opwf->winfile);
		return 0;
	}

	/* read subchunks until we encounter 'data' */
	do {
		// Read subchunk ID
		if (!ReadFile(opwf->winfile, &subchunk, sizeof(subchunk), &nread, NULL)) {
			sprintf(pcmwav_error, "Read error: this is not a correct PCM WAV file.\n");
			CloseHandle(opwf->winfile);
			return 0;
		}

		if (subchunk == 0x20746D66 /* 'fmt ' */) {
			// Read subchunk 1
			ReadFile(opwf->winfile, &fmt, sizeof(fmt), &nread, NULL);

			// Check it
			if (fmt.AudioFormat != 1) {
				sprintf(pcmwav_error, "Error in format subchunk: this is not a PCM WAV file.\n");
				CloseHandle(opwf->winfile);
				return 0;
			}

			opwf->bitspersample = fmt.BitsPerSample;

			if ((opwf->bitspersample != 8) && (opwf->bitspersample != 16)) {
				sprintf(pcmwav_error, "Can only deal with 8-bit or 16-bit samples.\n");
				CloseHandle(opwf->winfile);
				return 0;
			}

			// Skip any extra header bytes
			if (fmt.Subchunk1Size - 16)
				SetFilePointer(opwf->winfile, fmt.Subchunk1Size - 16, NULL, FILE_CURRENT);
			
			have_fmt = 1;
		} else if (subchunk != 0x61746164 /* 'data' */) {
			// unknown subchunk - read size and skip
			ReadFile(opwf->winfile, &subchunk_size, sizeof(subchunk_size), &nread, NULL);
			SetFilePointer(opwf->winfile, subchunk_size, NULL, FILE_CURRENT);
		}

	} while (subchunk != 0x61746164 /* 'data' */);

	if (!have_fmt) {
		sprintf(pcmwav_error, "Encountered data subchunk, but no format subchunk found.\n");
		CloseHandle(opwf->winfile);
		return 0;
	}

	/* read data chunk size */
	ReadFile(opwf->winfile, &opwf->ndatabytes, sizeof(opwf->ndatabytes), &nread, NULL);

	opwf->samplerate = fmt.SampleRate;
	opwf->nchannels = fmt.NumChannels;
	opwf->datapos = SetFilePointer(opwf->winfile, 0, NULL, FILE_CURRENT);
	
	return 1;
}

int pcmwav_read(pcmwavfile *pwf, void *buf, unsigned long len) {

	DWORD	nread;

	ReadFile(pwf->winfile, buf, len, &nread, NULL);

	if (nread != len) {
		sprintf(pcmwav_error, "Error in pcmwav_read(); only read %lu instead of %lu bytes.",
			nread, len);
		return 0;
	}
	
	return 1;
}

int pcmwav_write(pcmwavfile *pwf, void *buf, unsigned long len) {

	DWORD	nwritten;

	WriteFile(pwf->winfile, buf, len, &nwritten, NULL);

	if (nwritten != len) {
		sprintf(pcmwav_error, "Error in pcmwav_write(); only wrote %lu instead of %lu bytes.",
			nwritten, len);
		return 0;
	}
	
	return 1;
}

int pcmwav_rewind(pcmwavfile *pwf) {
	if (!SetFilePointer(pwf->winfile, pwf->datapos, NULL, FILE_BEGIN)) {
		sprintf(pcmwav_error, "Error in pcmwav_rewind().");
		return 0;
	}

	return 1;
}

int pcmwav_seek(pcmwavfile *pwf, long pos) {
	if (!SetFilePointer(pwf->winfile, pos, NULL, FILE_CURRENT)) {
		sprintf(pcmwav_error, "Error in pcmwav_seek() - pos = %ld", pos);
		return 0;
	}

	return 1;
}

int pcmwav_close(pcmwavfile *pwf) {
	CloseHandle(pwf->winfile);
	return 1;
}
