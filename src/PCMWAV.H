/*
	pcmwav.h - header file for PCM WAV I/O - v0.25
	(c) 2000-2002 Manuel Kasper <mk@neon1.net>

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
#include <windows.h>

#pragma pack(push, 1)

typedef struct {
	unsigned long	ChunkID;		// 'RIFF'
	unsigned long	ChunkSize;		
	unsigned long	Format;			// 'WAVE'
} RIFFhdr;

typedef struct {
	unsigned long	Subchunk1Size;
	unsigned short	AudioFormat;	// PCM = 1
	unsigned short	NumChannels;
	unsigned long	SampleRate;
	unsigned long	ByteRate;
	unsigned short	BlockAlign;
	unsigned short	BitsPerSample;
} fmt_sub;

typedef struct {
	unsigned short	nchannels;		// number of channels
	unsigned long	samplerate;		// sampling rate (e.g. 44100)
	unsigned long	bitspersample;	// bits per sample (8, 16)
	unsigned long	ndatabytes;		// number of data bytes in wave file

	// private variables
	HANDLE			winfile;		// file handle
	unsigned long	datapos;
} pcmwavfile;

#pragma pack(pop)

extern char pcmwav_error[];	// On error: contains a string that describes the error

// Opens a PCM WAV file and fills opwf with info; returns 1
// if successful or 0 on error
// access = GENERIC_READ or GENERIC_WRITE (or both)
int pcmwav_open(char *fname, DWORD access, pcmwavfile *opwf);

// Reads len data bytes (not samples!) into buf
int pcmwav_read(pcmwavfile *pwf, void *buf, unsigned long len);

// Writes len data bytes from buf
int pcmwav_write(pcmwavfile *pwf, void *buf, unsigned long len);

// Rewinds to start of data
int pcmwav_rewind(pcmwavfile *pwf);

// Seeks +/- pos in file
int pcmwav_seek(pcmwavfile *pwf, long pos);

// Closes PCM WAV file
int pcmwav_close(pcmwavfile *pwf);
