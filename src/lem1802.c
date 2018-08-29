#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <unistd.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "types.h"
#include "utils.h"
#include "exception.h"
#include "dcpu.h"
#include "lem1802.h"

struct device_lem1802 {
	u16 vramoff;
	u16 fontoff;
	u16 paletteoff;
	u8  bordercol;
};

enum lem1802_command {
	MEM_MAP_SCREEN,
	MEM_MAP_FONT,
	MEM_MAP_PALETTE,
	SET_BORDER_COLOUR,
};

const u16 lem1802_default_font[256] = {
    0xb79e, 0x388e, 0x722c, 0x75f4, 0x19bb, 0x7f8f, 0x85f9, 0xb158,
    0x242e, 0x2400, 0x082a, 0x0800, 0x0008, 0x0000, 0x0808, 0x0808,
    0x00ff, 0x0000, 0x00f8, 0x0808, 0x08f8, 0x0000, 0x080f, 0x0000,
    0x000f, 0x0808, 0x00ff, 0x0808, 0x08f8, 0x0808, 0x08ff, 0x0000,
    0x080f, 0x0808, 0x08ff, 0x0808, 0x6633, 0x99cc, 0x9933, 0x66cc,
    0xfef8, 0xe080, 0x7f1f, 0x0701, 0x0107, 0x1f7f, 0x80e0, 0xf8fe,
    0x5500, 0xaa00, 0x55aa, 0x55aa, 0xffaa, 0xff55, 0x0f0f, 0x0f0f,
    0xf0f0, 0xf0f0, 0x0000, 0xffff, 0xffff, 0x0000, 0xffff, 0xffff,
    0x0000, 0x0000, 0x005f, 0x0000, 0x0300, 0x0300, 0x3e14, 0x3e00,
    0x266b, 0x3200, 0x611c, 0x4300, 0x3629, 0x7650, 0x0002, 0x0100,
    0x1c22, 0x4100, 0x4122, 0x1c00, 0x1408, 0x1400, 0x081c, 0x0800,
    0x4020, 0x0000, 0x0808, 0x0800, 0x0040, 0x0000, 0x601c, 0x0300,
    0x3e49, 0x3e00, 0x427f, 0x4000, 0x6259, 0x4600, 0x2249, 0x3600,
    0x0f08, 0x7f00, 0x2745, 0x3900, 0x3e49, 0x3200, 0x6119, 0x0700,
    0x3649, 0x3600, 0x2649, 0x3e00, 0x0024, 0x0000, 0x4024, 0x0000,
    0x0814, 0x2241, 0x1414, 0x1400, 0x4122, 0x1408, 0x0259, 0x0600,
    0x3e59, 0x5e00, 0x7e09, 0x7e00, 0x7f49, 0x3600, 0x3e41, 0x2200,
    0x7f41, 0x3e00, 0x7f49, 0x4100, 0x7f09, 0x0100, 0x3e41, 0x7a00,
    0x7f08, 0x7f00, 0x417f, 0x4100, 0x2040, 0x3f00, 0x7f08, 0x7700,
    0x7f40, 0x4000, 0x7f06, 0x7f00, 0x7f01, 0x7e00, 0x3e41, 0x3e00,
    0x7f09, 0x0600, 0x3e41, 0xbe00, 0x7f09, 0x7600, 0x2649, 0x3200,
    0x017f, 0x0100, 0x3f40, 0x3f00, 0x1f60, 0x1f00, 0x7f30, 0x7f00,
    0x7708, 0x7700, 0x0778, 0x0700, 0x7149, 0x4700, 0x007f, 0x4100,
    0x031c, 0x6000, 0x0041, 0x7f00, 0x0201, 0x0200, 0x8080, 0x8000,
    0x0001, 0x0200, 0x2454, 0x7800, 0x7f44, 0x3800, 0x3844, 0x2800,
    0x3844, 0x7f00, 0x3854, 0x5800, 0x087e, 0x0900, 0x4854, 0x3c00,
    0x7f04, 0x7800, 0x447d, 0x4000, 0x2040, 0x3d00, 0x7f10, 0x6c00,
    0x417f, 0x4000, 0x7c18, 0x7c00, 0x7c04, 0x7800, 0x3844, 0x3800,
    0x7c14, 0x0800, 0x0814, 0x7c00, 0x7c04, 0x0800, 0x4854, 0x2400,
    0x043e, 0x4400, 0x3c40, 0x7c00, 0x1c60, 0x1c00, 0x7c30, 0x7c00,
    0x6c10, 0x6c00, 0x4c50, 0x3c00, 0x6454, 0x4c00, 0x0836, 0x4100,
    0x0077, 0x0000, 0x4136, 0x0800, 0x0201, 0x0201, 0x0205, 0x0200
};

const u16 lem1802_default_palette[] = {
    0x0000, 0x000a, 0x00a0, 0x00aa, 0x0a00, 0x0a0a, 0x0a50, 0x0aaa,
    0x0555, 0x055f, 0x05f5, 0x05ff, 0x0f55, 0x0f5f, 0x0ff5, 0x0fff
};

struct farbfeld_pixel {
	u16 r, g, b, a;
};

struct farbfeld_data {
	u64 magic;
	u32 width;
	u32 height;
	struct farbfeld_pixel pixels[];
};

#define LEM1802_FF_CELLWIDTH 4
#define LEM1802_FF_CELLHEIGHT 8
#define LEM1802_FF_ROWS 12
#define LEM1802_FF_COLS 32
#define LEM1802_FF_BORDERWIDTH 1
#define LEM1802_FF_PIXWIDTH (LEM1802_FF_CELLWIDTH * LEM1802_FF_COLS + 2)
#define LEM1802_FF_PIXHEIGHT (LEM1802_FF_CELLHEIGHT * LEM1802_FF_ROWS + 2)
#define LEM1802_FF_PIXSIZE (sizeof(struct farbfeld_pixel) * LEM1802_FF_PIXWIDTH * LEM1802_FF_PIXHEIGHT)
#define LEM1802_FF_SIZE (sizeof(struct farbfeld_data) + LEM1802_FF_PIXSIZE)
#define LEM1802_FF_INIT (struct farbfeld_data){ .width=LEM1802_FF_PIXWIDTH, .height=LEM1802_FF_PIXHEIGHT }

void lem1802_write(const char *filename, struct farbfeld_data *ffdat)
{
	size_t i;
	FILE *f = fopen(filename, "wb");
	u32 width = htonl(ffdat->width);
	u32 height = htonl(ffdat->height);
	u16 vals[4];

	fwrite("farbfeld", sizeof(char), 8, f);
	fwrite(&width, sizeof(u32), 1, f);
	fwrite(&height, sizeof(u32), 1, f);

	for (i = 0; i < ffdat->width * ffdat->height; i++) {
		memcpy(vals, &ffdat->pixels[i], sizeof(struct farbfeld_pixel));
		vals[0] = htons(vals[0]);
		vals[1] = htons(vals[1]);
		vals[2] = htons(vals[2]);
		vals[3] = htons(vals[3]);
		fwrite(&vals, sizeof(u16), 4, f);
	}

	/* fprintf(stderr, "%ld\n", ftell(f)); */

	fclose(f);
}

#define PIXEL(i, j) pixels[i * LEM1802_FF_PIXWIDTH + j]
#define COLOUR(x) (struct farbfeld_pixel){\
	.r = (palette[x & 0xf] >> 8) & 0xf,\
	.g = (palette[x & 0xf] >> 4) & 0xf,\
	.b = palette[x & 0xf] & 0xf,\
	.a = 0xffff}

void lem1802_draw_char(struct farbfeld_pixel pixels[], int i, int j, u16 vram, const u16 font[256], const u16 palette[16], int cycle)
{
	int x, y;
	u8 fg = (vram >> 12) & 0xf;
	u8 bg = (vram >> 8) & 0xf;
	u8 blink = (vram >> 7) & 0x1;
	u8 ch = vram & 0x7f;
	u32 glyph = (font[ch * 2] << 16) + font[ch * 2 + 1];

	for (x = 0; x < LEM1802_FF_CELLWIDTH; x++) {
		for (y = 0; y < LEM1802_FF_CELLHEIGHT; y++) {
			u16 a = i * LEM1802_FF_CELLHEIGHT + y + LEM1802_FF_BORDERWIDTH;
			u16 b = j * LEM1802_FF_CELLWIDTH + x + LEM1802_FF_BORDERWIDTH;

			if ((!blink || cycle) && ((glyph >> ((3 - x) * LEM1802_FF_CELLWIDTH + y)) & 0x1)) {
				PIXEL(a, b) = COLOUR(fg);
			} else {
				PIXEL(a, b) = COLOUR(bg);
			}
		}
	}
}

void lem1802_draw(struct farbfeld_pixel pixels[], const u16 vram[386], const u16 font[256], const u16 palette[16], u8 bordercol, int cycle)
{
	int i, j;

	/* fprintf(stderr, "%d %d\n", LEM1802_FF_PIXWIDTH, LEM1802_FF_PIXHEIGHT); */
	for (i = 0; i < LEM1802_FF_PIXHEIGHT; i++) {
		PIXEL(i, 0) = COLOUR(bordercol);
		PIXEL(i, LEM1802_FF_PIXHEIGHT - 1) = COLOUR(bordercol);
	}

	for (j = 0; j < LEM1802_FF_PIXHEIGHT; j++) {
		PIXEL(0, j) = COLOUR(bordercol);
		PIXEL(LEM1802_FF_PIXWIDTH - 1, j) = COLOUR(bordercol);
	}

	for (i = 0; i < LEM1802_FF_ROWS; i++) {
		for (j = 0; j < LEM1802_FF_COLS; j++) {
			lem1802_draw_char(pixels, i, j, vram[i * LEM1802_FF_ROWS + j], font, palette, cycle);
		}
	}
}

#undef COLOUR
#undef PIXEL

void lem1802_cycle(struct device *device, struct dcpu *dcpu)
{

	/*
	 * u16 vram[386];
	 * u16 font[256];
	 * u16 palette[16];
	 */

	const u16 *vram, *font, *palette;
	u16 vramoff = get_member_of(struct device_lem1802, device, vramoff);
	u16 fontoff = get_member_of(struct device_lem1802, device, fontoff);
	u16 paletteoff = get_member_of(struct device_lem1802, device, paletteoff);
	u8  bordercol = get_member_of(struct device_lem1802, device, bordercol);
	struct farbfeld_data *ffdat;
	
	ffdat = emalloc(LEM1802_FF_SIZE);
	*ffdat = LEM1802_FF_INIT;
	memset(ffdat->pixels, 0, LEM1802_FF_PIXSIZE);

	if (vramoff == 0)
		goto end;
	else
		vram = dcpu->ram + vramoff;

	if (fontoff == 0)
		font = lem1802_default_font;
	else
		font = dcpu->ram + fontoff;

	if (paletteoff == 0)
		palette = lem1802_default_palette;
	else
		palette = dcpu->ram + paletteoff;

	/* fprintf(stderr, "%p %p %p %ld %ld\n", ffdat, (char*)ffdat + sizeof *ffdat, ffdat->pixels, LEM1802_FF_SIZE, LEM1802_FF_PIXSIZE); */
	lem1802_draw(ffdat->pixels, vram, font, palette, bordercol, (dcpu->cycles / 100000) % 2);
end:
	lem1802_write("scratch.ff", ffdat);
	unlink("output.ff");
	int k  = symlink("scratch.ff", "output.ff");
	if (k != 0) {
		fprintf(stderr, "%s\n", strerror(errno));
	}
	free(ffdat);
}

void lem1802_interrupt(struct device *device, struct dcpu *dcpu)
{
	switch (dcpu->registers[0]) {
	case MEM_MAP_SCREEN:
		get_member_of(struct device_lem1802, device, vramoff)
			= dcpu->registers[1];
		break;
	case MEM_MAP_FONT:
		get_member_of(struct device_lem1802, device, fontoff)
			= dcpu->registers[1];
		break;
	case MEM_MAP_PALETTE:
		get_member_of(struct device_lem1802, device, paletteoff)
			= dcpu->registers[1];
		break;
	case SET_BORDER_COLOUR:
		get_member_of(struct device_lem1802, device, bordercol)
			= dcpu->registers[1];
		break;
	}
}

struct device *make_lem1802(void)
{
	char *data = ecalloc(sizeof(struct device) + sizeof(struct device_lem1802), 1);
	struct device *device = (struct device *)data;
	struct device_lem1802 *lem1802 = (struct device_lem1802*)(data + sizeof(struct device));

	*device = (struct device){
		.id=0x7349f615,
		.version=0x1802,
		.manufacturer=0x1c6c8b36,
		.interrupt=&lem1802_interrupt,
		.cycle=&lem1802_cycle,
		.data=&lem1802
	};

	return device;
}
