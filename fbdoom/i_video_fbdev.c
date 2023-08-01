// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "z_zone.h"

#include "tables.h"
#include "doomkeys.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

//#define CMAP256

struct fb_var_screeninfo fb = {};
int fb_scaling = 1;
int usemouse = 0;

struct color {
	uint32_t b:8;
	uint32_t g:8;
	uint32_t r:8;
	uint32_t a:8;
};

static struct color colors[256];

static int bayer2x2[2][2] = {
	{ 0, 2 },
	{ 3, 1 }
};

// The screen buffer; this is modified to draw things to the screen

byte *I_VideoBuffer = NULL;
byte *I_VideoBuffer_FB = NULL;

/* framebuffer file descriptor */
int fd_fb = 0;

int	X_width;
int X_height;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.

float mouse_acceleration = 2.0;
int mouse_threshold = 10;

// Gamma correction level to use

int usegamma = 0;

typedef struct
{
	byte r;
	byte g;
	byte b;
} col_t;

// Palette converted to RGB565

static uint16_t rgb565_palette[256];

void cmap_to_rgb565(uint16_t * out, uint8_t * in, int in_pixels)
{
	int i, j;
	struct color c;
	uint16_t r, g, b;

	for (i = 0; i < in_pixels; i++)
	{
		c = colors[*in]; 
		r = ((uint16_t)(c.r >> 3)) << 11;
		g = ((uint16_t)(c.g >> 2)) << 5;
		b = ((uint16_t)(c.b >> 3)) << 0;
		*out = (r | g | b);

		in++;
		for (j = 0; j < fb_scaling; j++) {
			out++;
		}
	}
}

void cmap_to_fb(uint8_t * out, uint8_t * in, int in_pixels)
{
	int i, j, k;
	struct color c;
	uint32_t pix;
	uint16_t r, g, b;

	for (i = 0; i < in_pixels; i++)
	{
		c = colors[*in];  /* R:8 G:8 B:8 format! */
		r = (uint16_t)(c.r >> (8 - fb.red.length));
		g = (uint16_t)(c.g >> (8 - fb.green.length));
		b = (uint16_t)(c.b >> (8 - fb.blue.length));
		pix = r << fb.red.offset;
		pix |= g << fb.green.offset;
		pix |= b << fb.blue.offset;

		for (k = 0; k < fb_scaling; k++) {
			for (j = 0; j < fb.bits_per_pixel/8; j++) {
				*out = (pix >> (j*8));
				out++;
			}
		}
		in++;
	}
}

void I_InitGraphics (void)
{
	int i;

	/* Open fbdev file descriptor */
	fd_fb = open("/dev/fb1", O_RDWR);
	if (fd_fb < 0)
	{
		printf("Could not open /dev/fb0");
		exit(-1);
	}

	/* fetch framebuffer info */
	ioctl(fd_fb, FBIOGET_VSCREENINFO, &fb);
	/* change params if needed */
	printf("I_InitGraphics: framebuffer: x_res: %d, y_res: %d, x_virtual: %d, y_virtual: %d, bpp: %d, grayscale: %d\n",
			fb.xres, fb.yres, fb.xres_virtual, fb.yres_virtual, fb.bits_per_pixel, fb.grayscale);

	printf("I_InitGraphics: framebuffer: RGBA: %d%d%d%d, red_off: %d, green_off: %d, blue_off: %d, transp_off: %d\n",
			fb.red.length, fb.green.length, fb.blue.length, fb.transp.length, fb.red.offset, fb.green.offset, fb.blue.offset, fb.transp.offset);

	printf("I_InitGraphics: DOOM screen size: w x h: %d x %d\n", SCREENWIDTH, SCREENHEIGHT);


	i = M_CheckParmWithArgs("-scaling", 1);
	if (i > 0) {
		i = atoi(myargv[i + 1]);
		fb_scaling = i;
		printf("I_InitGraphics: Scaling factor: %d\n", fb_scaling);
	} else {
		fb_scaling = fb.xres / SCREENWIDTH;
		if (fb.yres / SCREENHEIGHT < fb_scaling)
			fb_scaling = fb.yres / SCREENHEIGHT;
		printf("I_InitGraphics: Auto-scaling factor: %d\n", fb_scaling);
	}

	/* Allocate screen to draw to */
	I_VideoBuffer = (byte*)Z_Malloc (SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);  // For DOOM to draw on
	I_VideoBuffer_FB = (byte*)malloc(fb.xres * fb.yres * (fb.bits_per_pixel/8));     // For a single write() syscall to fbdev

	screenvisible = true;

	extern int I_InitInput(void);
	I_InitInput();
}

void I_ShutdownGraphics (void)
{
	Z_Free (I_VideoBuffer);
	free(I_VideoBuffer_FB);
}

void I_StartFrame (void) {}

__attribute__ ((weak)) void I_GetEvent (void) {}

_attribute__ ((weak)) void I_StartTic (void)
{
	I_GetEvent();
}

void I_UpdateNoBlit (void) {}

//
// I_FinishUpdate
//

void I_FinishUpdate (void)
{
	int y;
	int x_offset, y_offset, x_offset_end;
	unsigned char *line_in, *line_out;

	/* Offsets in case FB is bigger than DOOM */
	/* 600 = fb heigt, 200 screenheight */
	/* 600 = fb heigt, 200 screenheight */
	/* 2048 =fb width, 320 screenwidth */
	y_offset     = (((fb.yres - (SCREENHEIGHT * fb_scaling)) * fb.bits_per_pixel/8)) / 2;
	x_offset     = (((fb.xres - (SCREENWIDTH  * fb_scaling)) * fb.bits_per_pixel/8)) / 2; // XXX: siglent FB hack: /4 instead of /2, since it seems to handle the resolution in a funny way
	//x_offset     = 0;
	x_offset_end = ((fb.xres - (SCREENWIDTH  * fb_scaling)) * fb.bits_per_pixel/8) - x_offset;

	/* DRAW SCREEN */
	line_in  = (unsigned char *) I_VideoBuffer;
	line_out = (unsigned char *) I_VideoBuffer_FB;

	y = SCREENHEIGHT;

	while (y--)
	{
		int i;
		for (i = 0; i < fb_scaling; i++) {
			line_out += x_offset;
			
			#ifdef CMAP256
				for (fb_scaling == 1) {
					memcpy(line_out, line_in, SCREENWIDTH); /* fb_width is bigger than Doom SCREENWIDTH... */
				} else {
					//XXX FIXME fb_scaling support!
				}
			#else
				cmap_to_fb((void*)line_out, (void*)line_in, SCREENWIDTH);
			#endif
			
			line_out += (SCREENWIDTH * fb_scaling * (fb.bits_per_pixel/8)) + x_offset_end;
		}
		line_in += SCREENWIDTH;
	}
	
	/* Start drawing from y-offset */
	lseek(fd_fb, y_offset * fb.xres, SEEK_SET);

	// Apply dithering
	for (int y = 0; y < SCREENHEIGHT; y++)
	{
		int i;
		for (i = 0; i < fb_scaling; i++)
		{
			line_out += x_offset;

			// Process each pixel in the current row
			for (int x = 0; x < SCREENWIDTH; x++)
			{
				struct color c = colors[line_in[x]];

				// Convert RGB to grayscale (average of R, G, and B)
				int gray = (c.r + c.g + c.b) / 3;

				// Use Bayer 2x2 dithering to decide if pixel should be black or white
				int dithered = gray > bayer2x2[x % 2][y % 2] * 64 ? 255 : 0;

				// Set the pixel in the output buffer
				for (int j = 0; j < fb_scaling; j++)
				{
					for (int k = 0; k < fb.bits_per_pixel / 8; k++)
					{
						*line_out = dithered;
						line_out++;
					}
				}
			}

			line_out += (SCREENWIDTH * fb_scaling * (fb.bits_per_pixel / 8)) + x_offset_end;
		}
		line_in += SCREENWIDTH;
	}

	// Draw portion used by doom + x-offsets 
	write(fd_fb, I_VideoBuffer_FB, (SCREENHEIGHT * fb_scaling * (fb.bits_per_pixel/8)) * fb.xres); 
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
	memcpy (scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
#define GFX_RGB565(r, g, b)			((((r & 0xF8) >> 3) << 11) | (((g & 0xFC) >> 2) << 5) | ((b & 0xF8) >> 3))
#define GFX_RGB565_R(color)			((0xF800 & color) >> 11)
#define GFX_RGB565_G(color)			((0x07E0 & color) >> 5)
#define GFX_RGB565_B(color)			(0x001F & color)

void I_SetPalette (byte* palette)
{
	int i;

	/* performance boost:
	 * map to the right pixel format over here! */

	for (i=0; i<256; ++i ) {
		colors[i].a = 0;
		colors[i].r = gammatable[usegamma][*palette++];
		colors[i].g = gammatable[usegamma][*palette++];
		colors[i].b = gammatable[usegamma][*palette++];
	}

	/* Set new color map in kernel framebuffer driver */
	//XXX FIXME ioctl(fd_fb, IOCTL_FB_PUTCMAP, colors);
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex (int r, int g, int b)
{
	int best, best_diff, diff;
	int i;
	col_t color;

	printf("I_GetPaletteIndex\n");

	best = 0;
	best_diff = INT_MAX;

	for (i = 0; i < 256; ++i)
	{
		color.r = GFX_RGB565_R(rgb565_palette[i]);
		color.g = GFX_RGB565_G(rgb565_palette[i]);
		color.b = GFX_RGB565_B(rgb565_palette[i]);

		diff = (r - color.r) * (r - color.r)
			 + (g - color.g) * (g - color.g)
			 + (b - color.b) * (b - color.b);

		if (diff < best_diff)
		{
			best = i;
			best_diff = diff;
		}

		if (diff == 0)
		{
			break;
		}
	}

	return best;
}

void I_BeginRead (void) {}
void I_EndRead (void) {}
void I_SetWindowTitle (char *title) {}
void I_GraphicsCheckCommandLine (void) {}
void I_SetGrabMouseCallback (grabmouse_callback_t func) {}
void I_EnableLoadingDisk(void) {}
void I_BindVideoVariables (void) {}
void I_DisplayFPSDots (boolean dots_on) {}
void I_CheckIsScreensaver (void) {}
