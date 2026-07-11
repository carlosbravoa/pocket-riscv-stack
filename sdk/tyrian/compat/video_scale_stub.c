/*
 * video_scale_stub.c — replaces src/video_scale.c for the riscv-stack port.
 *
 * The Pocket panel takes the game's native 320x200 indexed frame directly
 * (video.c pushes it via RVSDL_PresentIndexed); the RGB software scalers are
 * dead weight here. config.c/opentyr.c still reference the scaler table for
 * the video menu, so provide a one-entry table.
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include "video_scale.h"

#include <string.h>

uint scaler = 0;

const struct Scalers scalers[] = {
	{ 320, 200, NULL, NULL, "None" },
};
const uint scalers_count = 1;

void set_scaler_by_name(const char *name)
{
	for (uint i = 0; i < scalers_count; ++i) {
		if (strcmp(name, scalers[i].name) == 0) {
			scaler = i;
			break;
		}
	}
}
