/*
 * id_in_rvstack.c — Omnispeak IN_Backend on the riscv-stack HAL.
 *
 * The pad is presented as the DOS keyboard: pumpEvents() diffs the pad
 * bitmap and synthesizes IN_HandleKeyDown/Up with Keen's own default
 * bindings (so IN_ctrl_Keyboard1 and every menu works untouched):
 *
 *   d-pad  -> arrows        move / menu navigation
 *   A      -> LCtrl         jump  (Keen default in_kbd_jump)
 *   B      -> LAlt          pogo  (Keen default in_kbd_pogo)
 *   X      -> Space         shoot (Keen default in_kbd_fire)
 *   Y      -> Y             yes/no prompts ("Y" answers them)
 *   L1     -> F1            help
 *   R1     -> N             the "no" half of Y/N prompts
 *   START  -> Enter         menu confirm / status screen
 *   SELECT -> Escape        menu open / back out
 *   SELECT+START (held together) -> flush saves, exit to the game picker
 *
 * No joystick is reported (the pad IS the keyboard); no text input.
 *
 * Part of the Omnispeak riscv-stack port glue. SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hal.h" /* FIRST (trap #2) */
#include "rv_keen.h"

#include <string.h>

#include "id_in.h"

/* pad-bit order: matches HAL_BTN_* bit numbers */
static const IN_ScanCode in_rv_map[16] = {
	IN_SC_UpArrow,    /* 0  up    */
	IN_SC_DownArrow,  /* 1  down  */
	IN_SC_LeftArrow,  /* 2  left  */
	IN_SC_RightArrow, /* 3  right */
	IN_SC_Control,    /* 4  A     */
	IN_SC_Alt,        /* 5  B     */
	IN_SC_Space,      /* 6  X     */
	IN_SC_Y,          /* 7  Y     */
	IN_SC_F1,         /* 8  L1    */
	IN_SC_N,          /* 9  R1    */
	IN_SC_None,       /* 10 L2    */
	IN_SC_None,       /* 11 R2    */
	IN_SC_None,       /* 12 L3    */
	IN_SC_None,       /* 13 R3    */
	IN_SC_Escape,     /* 14 SELECT */
	IN_SC_Enter,      /* 15 START  */
};

static uint32_t in_rv_lastButtons;

static void IN_RV_PumpEvents(void)
{
	RVK_TimerPump();
	fb_flip_poll();

	input_poll();
	uint32_t b = input_buttons(0);

	/* the platform quit convention */
	if ((b & HAL_BTN_SELECT) && (b & HAL_BTN_START))
		RVK_QuitToPicker();

	uint32_t changed = b ^ in_rv_lastButtons;
	in_rv_lastButtons = b;
	if (!changed)
		return;

	for (int i = 0; i < 16; i++)
	{
		if (!(changed & (1u << i)) || in_rv_map[i] == IN_SC_None)
			continue;
		if (b & (1u << i))
			IN_HandleKeyDown(in_rv_map[i], false);
		else
			IN_HandleKeyUp(in_rv_map[i], false);
	}
}

static void IN_RV_WaitKey(void)
{
	IN_SetLastScan(IN_SC_None);
	while (IN_GetLastScan() == IN_SC_None)
	{
		IN_RV_PumpEvents();
		sys_delay_us(1000);
	}
}

static void IN_RV_Startup(bool disableJoysticks)
{
	(void)disableJoysticks;
	input_poll();
	in_rv_lastButtons = input_buttons(0);
}

static bool IN_RV_StartJoy(int joystick)
{
	(void)joystick;
	return false;
}

static void IN_RV_StopJoy(int joystick)
{
	(void)joystick;
}

static bool IN_RV_JoyPresent(int joystick)
{
	(void)joystick;
	return false;
}

static void IN_RV_JoyGetAbs(int joystick, int *x, int *y)
{
	(void)joystick;
	if (x)
		*x = 0;
	if (y)
		*y = 0;
}

static uint16_t IN_RV_JoyGetButtons(int joystick)
{
	(void)joystick;
	return 0;
}

static const char *IN_RV_JoyGetName(int joystick)
{
	(void)joystick;
	return 0;
}

IN_Backend in_rvstack_backend = {
	.startup = IN_RV_Startup,
	.shutdown = 0,
	.pumpEvents = IN_RV_PumpEvents,
	.waitKey = IN_RV_WaitKey,
	.joyStart = IN_RV_StartJoy,
	.joyStop = IN_RV_StopJoy,
	.joyPresent = IN_RV_JoyPresent,
	.joyGetAbs = IN_RV_JoyGetAbs,
	.joyGetButtons = IN_RV_JoyGetButtons,
	.joyGetName = IN_RV_JoyGetName,
	.joyGetButtonName = 0,
	.startTextInput = 0,
	.stopTextInput = 0,
	.joyAxisMin = -1000,
	.joyAxisMax = 1000,
	.supportsTextEvents = false,
};

IN_Backend *IN_Impl_GetBackend()
{
	return &in_rvstack_backend;
}
