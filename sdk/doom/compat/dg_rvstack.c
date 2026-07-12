/*
 * dg_rvstack.c — the doomgeneric platform seam over the riscv-stack HAL.
 *
 * doomgeneric funnels every platform dependency through six functions
 * (DG_Init / DG_DrawFrame / DG_SleepMs / DG_GetTicksMs / DG_GetKey /
 * DG_SetWindowTitle); this file is the whole video/input/timing backend:
 *
 *  - Video: the game is built with -DCMAP256 -DDOOMGENERIC_RESX=320
 *    -DDOOMGENERIC_RESY=200, so DG_ScreenBuffer is 320x200 of PLAYPAL
 *    indices and I_SetPalette leaves the RGB palette in i_video.c's
 *    `colors[]` + raises `palette_changed`. DG_DrawFrame letterboxes it
 *    onto the 320x240 backbuffer and forwards the palette to the hardware
 *    CLUT via palette_set(). Zero pixel-format conversion on the CPU.
 *  - Input: pad edges become Doom keycodes (doomkeys.h) in a small queue,
 *    exactly like the upstream SDL backend's key queue. The PC twin's
 *    keyboard arrives through the same pad bits (hal_pc.c maps it).
 *  - Timing: sys_ticks_us()/1000. Wraps with the 32-bit us counter
 *    (~71 min); Doom only ever subtracts, so a wrap costs one hiccup.
 *
 * hal.h FIRST, before any header that might #define conflicting names
 * (the Tyrian port's hard-won include-order rule).
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#include "hal.h"

#include "pakfs.h"

#include <stdio.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_video.h"                    /* colors[], palette_changed (CMAP256) */

#include "rvfile.h"                     /* rvfs_files_init() */

/* Doom's pak (doom1.wad inside) is ~4.2 MB — bigger than the default 3 MB
 * pak window at main_ram+0x100000, which also sits BELOW the game image.
 * Land it above the 28 MB game region + the save-staging MB instead, the
 * same spot the Tyrian port proved out (of_files.c):
 *   0x40400000 +28MB = 0x42000000  game image + heap
 *   0x42000000 + 1MB = 0x42100000  HAL save staging
 *   0x42100000 ..                  doom.pak lands HERE
 * On the PC twin pak_open_at() ignores the offset and reads ./game.pak or
 * $RVSTACK_PAK from disk. */
#define DOOM_PAK_OFFSET 0x02100000u

#define SCREEN_W 320
#define SCREEN_H 200

/* ------------------------------------------------------------------ keys */

#define KEYQUEUE_SIZE 16
static unsigned short key_queue[KEYQUEUE_SIZE];
static unsigned int   key_wi, key_ri;

static void key_push(int pressed, unsigned char key)
{
	key_queue[key_wi++ % KEYQUEUE_SIZE] = (unsigned short)((pressed << 8) | key);
}

/* Pad -> Doom key map. The task-fixed core: d-pad=arrows, A=fire(ctrl),
 * B=use(space), Start=enter, Select=tab(automap). Doom's title screen only
 * reacts to ESCAPE (menu) and the quit/confirm prompts want 'y', neither of
 * which the fixed map reaches — X and Y cover them. L1/R1 strafe. */
static const struct { uint32_t bit; unsigned char key; } padmap[] = {
	{ HAL_BTN_UP,     KEY_UPARROW    },
	{ HAL_BTN_DOWN,   KEY_DOWNARROW  },
	{ HAL_BTN_LEFT,   KEY_LEFTARROW  },
	{ HAL_BTN_RIGHT,  KEY_RIGHTARROW },
	{ HAL_BTN_A,      KEY_FIRE       },  /* ctrl  */
	{ HAL_BTN_B,      KEY_USE        },  /* space */
	{ HAL_BTN_X,      KEY_ESCAPE     },  /* menu  */
	{ HAL_BTN_Y,      'y'            },  /* confirm prompts */
	{ HAL_BTN_L1,     KEY_STRAFE_L   },
	{ HAL_BTN_R1,     KEY_STRAFE_R   },
	{ HAL_BTN_SELECT, KEY_TAB        },  /* automap */
	{ HAL_BTN_START,  KEY_ENTER      },
};

static void pump_pad(void)
{
	static uint32_t prev;
	input_poll();
	uint32_t cur  = input_buttons(0) | input_buttons(1);
	uint32_t diff = cur ^ prev;
	if (!diff) {
		/* SELECT+START = back to the game picker (SDK convention) */
		if ((cur & (HAL_BTN_SELECT | HAL_BTN_START))
		        == (HAL_BTN_SELECT | HAL_BTN_START))
			sys_exit();
		return;
	}
	for (unsigned i = 0; i < sizeof(padmap) / sizeof(padmap[0]); i++)
		if (diff & padmap[i].bit)
			key_push(!!(cur & padmap[i].bit), padmap[i].key);
	prev = cur;
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
	if (key_ri == key_wi)               /* queue empty: sample the pads */
		pump_pad();
	if (key_ri == key_wi)
		return 0;
	unsigned short data = key_queue[key_ri++ % KEYQUEUE_SIZE];
	*pressed = data >> 8;
	*doomKey = data & 0xFF;
	return 1;
}

/* ------------------------------------------------------------------ video */

void DG_Init(void)
{
	/* sys_init() already ran in main() — nothing left to bring up. */
}

void DG_DrawFrame(void)
{
	if (palette_changed) {              /* PLAYPAL -> hardware CLUT */
		static uint8_t rgb[256][3];
		for (int i = 0; i < 256; i++) {
			rgb[i][0] = (uint8_t)colors[i].r;
			rgb[i][1] = (uint8_t)colors[i].g;
			rgb[i][2] = (uint8_t)colors[i].b;
		}
		palette_set((const uint8_t (*)[3])rgb);
		palette_changed = false;
	}

	uint8_t *fb = fb_backbuffer();
	int fbw = fb_width(), fbh = fb_height();
	int y0 = (fbh - SCREEN_H) / 2;      /* 320x200 letterboxed on 320x240 */

	memset(fb, 0, (size_t)(y0 * fbw));  /* index 0 = black in PLAYPAL */
	memset(fb + (y0 + SCREEN_H) * fbw, 0, (size_t)((fbh - y0 - SCREEN_H) * fbw));

	const uint8_t *src = (const uint8_t *)DG_ScreenBuffer;
	uint8_t *dst = fb + y0 * fbw + (fbw - SCREEN_W) / 2;
	for (int y = 0; y < SCREEN_H; y++)
		memcpy(dst + y * fbw, src + y * SCREEN_W, SCREEN_W);

	fb_present();
}

/* ------------------------------------------------------------------ misc */

void DG_SleepMs(uint32_t ms)
{
	fb_flip_poll();                     /* let a deferred flip complete */
	sys_delay_us(ms * 1000u);
}

uint32_t DG_GetTicksMs(void)
{
	return sys_ticks_us() / 1000u;
}

void DG_SetWindowTitle(const char *title)
{
	(void)title;                        /* one fixed screen, no titlebar */
}

/* ------------------------------------------------------------------ main */

int main(void)
{
	static char *argv[] = { "doom", "-iwad", "doom1.wad", 0 };

	sys_init();

	/* Mount the pak above the game region (see DOOM_PAK_OFFSET). Failure is
	 * survivable here — D_DoomMain reports the missing IWAD with text. */
	if (pakfs_mount_at(DOOM_PAK_OFFSET) != 0)
		printf("doom: pakfs mount failed — no doom.pak picked?\n");
	rvfs_files_init();

	doomgeneric_Create(3, argv);
	for (;;)
		doomgeneric_Tick();
	return 0;
}
