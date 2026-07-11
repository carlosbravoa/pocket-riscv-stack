/*
 * sdl2_shim.c — SDL2 API pieces for the OpenTyrian2000 riscv-stack port.
 *
 * Implements the SDL2 entry points from compat/SDL.h that don't resolve
 * directly to sdl_lite symbols: subsystem bookkeeping, the event translator
 * (over compat/lite_bridge.c), audio device wrappers, the S8->S16 resampler
 * (SDL_AudioCVT), scancode names, and small stubs for a machine with one
 * fixed screen and no mouse.
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include "SDL.h"
#include "rv_bridge.h"

#include <string.h>

/* --------------------------------------------------------- subsystems --- */

static Uint32 init_mask;

int SDL_InitSubSystem(Uint32 flags)
{
	if (flags & SDL_INIT_VIDEO)
		rvb_video_init();
	init_mask |= flags;
	return 0;
}

void SDL_QuitSubSystem(Uint32 flags)
{
	if (flags & SDL_INIT_AUDIO)
		rvb_audio_close();
	init_mask &= ~flags;
}

Uint32 SDL_WasInit(Uint32 flags)
{
	if (flags == 0)
		flags = 0xFFFFFFFFu;
	return init_mask & flags;
}

const char *SDL_GetError(void)
{
	return "";
}

int SDL_SetHint(const char *name, const char *value)
{
	(void)name; (void)value;
	return 0;
}

/* ------------------------------------------------------------- video --- */

Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b)
{
	(void)f;                            /* one format: XRGB8888 */
	return ((Uint32)r << 16) | ((Uint32)g << 8) | b;
}

const char *SDL_GetPixelFormatName(Uint32 format)
{
	(void)format;
	return "SDL_PIXELFORMAT_RGB888";
}

int SDL_GetNumVideoDisplays(void) { return 1; }
int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
int SDL_SetRelativeMouseMode(SDL_bool enabled) { (void)enabled; return 0; }

void RVSDL_InitVideo(void)
{
	rvb_video_init();
}

void RVSDL_PresentIndexed(const void *pixels, int pitch, int w, int h,
                          const void *colors256)
{
	rvb_present_indexed(pixels, pitch, w, h, colors256);
}

/* ------------------------------------------------------------ events --- */

#define EVQ_SIZE 16
static SDL_Event evq[EVQ_SIZE];
static int evq_head, evq_tail;

int SDL2C_PushEvent(const SDL_Event *ev)
{
	int next = (evq_tail + 1) % EVQ_SIZE;
	if (next == evq_head)
		return -1;                      /* full */
	evq[evq_tail] = *ev;
	evq_tail = next;
	return 1;
}

int SDL2C_PollEvent(SDL_Event *ev)
{
	if (evq_head != evq_tail) {
		if (ev)
			*ev = evq[evq_head];
		evq_head = (evq_head + 1) % EVQ_SIZE;
		return 1;
	}
	int sc = 0;
	int r = rvb_poll_key(&sc);
	if (r == 0)
		return 0;
	if (ev) {
		memset(ev, 0, sizeof(*ev));
		ev->type = (r == 1) ? SDL_KEYDOWN : SDL_KEYUP;
		ev->key.state = (r == 1) ? SDL_PRESSED : SDL_RELEASED;
		ev->key.keysym.scancode = (SDL_Scancode)sc;
		ev->key.keysym.mod = KMOD_NONE;
	}
	return 1;
}

SDL_Keymod SDL_GetModState(void)
{
	return KMOD_NONE;                   /* pads have no modifiers */
}

/* --------------------------------------------------------- keyboard --- */

typedef struct { SDL_Scancode sc; const char *name; } sc_name_t;

static const sc_name_t sc_names[] = {
	{ SDL_SCANCODE_UP, "Up" }, { SDL_SCANCODE_DOWN, "Down" },
	{ SDL_SCANCODE_LEFT, "Left" }, { SDL_SCANCODE_RIGHT, "Right" },
	{ SDL_SCANCODE_SPACE, "Space" }, { SDL_SCANCODE_RETURN, "Return" },
	{ SDL_SCANCODE_ESCAPE, "Escape" }, { SDL_SCANCODE_BACKSPACE, "Backspace" },
	{ SDL_SCANCODE_TAB, "Tab" }, { SDL_SCANCODE_LCTRL, "Left Ctrl" },
	{ SDL_SCANCODE_RCTRL, "Right Ctrl" }, { SDL_SCANCODE_LSHIFT, "Left Shift" },
	{ SDL_SCANCODE_RSHIFT, "Right Shift" }, { SDL_SCANCODE_LALT, "Left Alt" },
	{ SDL_SCANCODE_RALT, "Right Alt" }, { SDL_SCANCODE_INSERT, "Insert" },
	{ SDL_SCANCODE_DELETE, "Delete" }, { SDL_SCANCODE_HOME, "Home" },
	{ SDL_SCANCODE_END, "End" }, { SDL_SCANCODE_PAGEUP, "PageUp" },
	{ SDL_SCANCODE_PAGEDOWN, "PageDown" },
	{ SDL_SCANCODE_A, "A" }, { SDL_SCANCODE_B, "B" }, { SDL_SCANCODE_C, "C" },
	{ SDL_SCANCODE_D, "D" }, { SDL_SCANCODE_E, "E" }, { SDL_SCANCODE_F, "F" },
	{ SDL_SCANCODE_G, "G" }, { SDL_SCANCODE_H, "H" }, { SDL_SCANCODE_I, "I" },
	{ SDL_SCANCODE_J, "J" }, { SDL_SCANCODE_K, "K" }, { SDL_SCANCODE_L, "L" },
	{ SDL_SCANCODE_M, "M" }, { SDL_SCANCODE_N, "N" }, { SDL_SCANCODE_O, "O" },
	{ SDL_SCANCODE_P, "P" }, { SDL_SCANCODE_Q, "Q" }, { SDL_SCANCODE_R, "R" },
	{ SDL_SCANCODE_S, "S" }, { SDL_SCANCODE_T, "T" }, { SDL_SCANCODE_U, "U" },
	{ SDL_SCANCODE_V, "V" }, { SDL_SCANCODE_W, "W" }, { SDL_SCANCODE_X, "X" },
	{ SDL_SCANCODE_Y, "Y" }, { SDL_SCANCODE_Z, "Z" },
	{ SDL_SCANCODE_1, "1" }, { SDL_SCANCODE_2, "2" }, { SDL_SCANCODE_3, "3" },
	{ SDL_SCANCODE_4, "4" }, { SDL_SCANCODE_5, "5" }, { SDL_SCANCODE_6, "6" },
	{ SDL_SCANCODE_7, "7" }, { SDL_SCANCODE_8, "8" }, { SDL_SCANCODE_9, "9" },
	{ SDL_SCANCODE_0, "0" },
	{ SDL_SCANCODE_F1, "F1" }, { SDL_SCANCODE_F2, "F2" },
	{ SDL_SCANCODE_F3, "F3" }, { SDL_SCANCODE_F4, "F4" },
	{ SDL_SCANCODE_F5, "F5" }, { SDL_SCANCODE_F6, "F6" },
	{ SDL_SCANCODE_F7, "F7" }, { SDL_SCANCODE_F8, "F8" },
	{ SDL_SCANCODE_F9, "F9" }, { SDL_SCANCODE_F10, "F10" },
	{ SDL_SCANCODE_F11, "F11" }, { SDL_SCANCODE_F12, "F12" },
};

const char *SDL_GetScancodeName(SDL_Scancode sc)
{
	for (unsigned i = 0; i < sizeof(sc_names) / sizeof(sc_names[0]); i++)
		if (sc_names[i].sc == sc)
			return sc_names[i].name;
	return "";
}

SDL_Scancode SDL_GetScancodeFromName(const char *name)
{
	if (!name)
		return SDL_SCANCODE_UNKNOWN;
	for (unsigned i = 0; i < sizeof(sc_names) / sizeof(sc_names[0]); i++)
		if (strcmp(sc_names[i].name, name) == 0)
			return sc_names[i].sc;
	return SDL_SCANCODE_UNKNOWN;
}

size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen)
{
	size_t srclen = strlen(src);
	if (maxlen > 0) {
		size_t n = (srclen < maxlen - 1) ? srclen : maxlen - 1;
		memcpy(dst, src, n);
		dst[n] = 0;
	}
	return srclen;
}

/* ------------------------------------------------------------- audio --- */

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                      const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained,
                                      int allowed_changes)
{
	(void)device; (void)iscapture; (void)allowed_changes;
	int freq = rvb_audio_open(desired->channels, desired->samples,
	                          desired->callback, desired->userdata);
	if (freq < 0)
		return 0;
	if (obtained) {
		*obtained = *desired;
		obtained->freq    = freq;       /* shim always runs 48 kHz */
		obtained->format  = AUDIO_S16SYS;
		obtained->silence = 0;
		if (obtained->samples == 0 || obtained->samples > 512)
			obtained->samples = 256;
		obtained->size = (Uint32)obtained->samples * obtained->channels * 2;
	}
	return 2;                           /* any nonzero device id */
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
	(void)dev;
	rvb_audio_pause(pause_on);
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
	(void)dev;
	rvb_audio_close();
}

/* No audio thread exists: the callback runs synchronously inside
 * SDL_Flip()/SDL_Delay(), so there is nothing to lock against. */
void SDL_LockAudioDevice(SDL_AudioDeviceID dev)   { (void)dev; }
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }

/* S8 mono -> S16 mono rate conversion (11025 -> 48000), the one conversion
 * nortsong.c performs. In-place in cvt->buf, processed backward so the
 * 16-bit output never clobbers unread 8-bit input. */

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
                      SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
                      SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate)
{
	if (src_format != AUDIO_S8 || dst_format != AUDIO_S16SYS ||
	    src_channels != 1 || dst_channels != 1 ||
	    src_rate <= 0 || dst_rate <= 0)
		return -1;                      /* only the game's one conversion */
	memset(cvt, 0, sizeof(*cvt));
	cvt->src_format = src_format;
	cvt->dst_format = dst_format;
	cvt->src_rate   = src_rate;
	cvt->dst_rate   = dst_rate;
	cvt->needed     = 1;
	/* out bytes per in byte: 2 (S8->S16) * ceil(rate ratio), plus slack */
	cvt->len_mult   = 2 * ((dst_rate + src_rate - 1) / src_rate + 1);
	cvt->len_ratio  = (double)cvt->len_mult / 2.0;
	return 1;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
	if (!cvt->needed || !cvt->buf || cvt->len <= 0)
		return -1;

	const Sint32 in_n  = cvt->len;      /* S8 samples */
	const Sint32 out_n = (Sint32)(((Sint64)in_n * cvt->dst_rate) / cvt->src_rate);
	Sint8  *in  = (Sint8 *)cvt->buf;
	Sint16 *out = (Sint16 *)cvt->buf;

	/* 16.16 fixed-point source step */
	const Uint32 step = (Uint32)(((Uint64)cvt->src_rate << 16) / (Uint32)cvt->dst_rate);

	for (Sint32 j = out_n - 1; j >= 0; j--) {
		Uint32 pos = (Uint32)j * step;
		Sint32 i   = (Sint32)(pos >> 16);
		Sint32 f   = (Sint32)(pos & 0xFFFF);
		Sint32 a   = in[i];
		Sint32 b   = (i + 1 < in_n) ? in[i + 1] : a;
		Sint32 s   = (a << 8) + (((b - a) * f) >> 8);   /* lerp, scale to S16 */
		if (s > 32767)  s = 32767;
		if (s < -32768) s = -32768;
		out[j] = (Sint16)s;
	}

	cvt->len_cvt = out_n * 2;
	return 0;
}
