// hal_pc — the console's hal.h implemented on desktop SDL2: compile any SDK
// game natively and iterate at PC speed (idea proven by openfpgaSDK's PC
// twin). Same API, same 320x240 8bpp palettized model, keyboard as the pad.
//
//   make -C sdk/<game> -f ../pc/pc.mk        -> <game>-pc (native binary)
//
// Fidelity notes (documented, deliberate):
// - Timing is wall-clock; fb_present paces to ~60 Hz.
// - pak_open() loads <game dir>/game.pak or $RVSTACK_PAK from disk.
// - save_open()/save_commit() persist to ./<name>.sav files immediately
//   (the Pocket persists at core quit/sleep — PC is stricter, not looser).
// - sys_exit() exits the process.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "hal.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240
#define SCALE 3

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static uint8_t  fbmem[2][W * H];
static int      drawpage;
static uint32_t palette[256];
static uint64_t t0_us;

// ---------------------------------------------------------------- system
static uint64_t now_us(void)
{
	return SDL_GetPerformanceCounter() * 1000000ull
	     / SDL_GetPerformanceFrequency();
}

void sys_init(void)
{
	if (win)
		return;
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	win = SDL_CreateWindow("riscv-stack (PC twin)", SDL_WINDOWPOS_CENTERED,
	                       SDL_WINDOWPOS_CENTERED, W * SCALE, H * SCALE, 0);
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
	tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
	                        SDL_TEXTUREACCESS_STREAMING, W, H);
	for (int i = 0; i < 256; i++) {                 // rgb332 identity default
		uint8_t r = (i >> 5) * 255 / 7, g = ((i >> 2) & 7) * 255 / 7,
		        b = (i & 3) * 255 / 3;
		palette[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
	}
	t0_us = now_us();
}

uint32_t sys_ticks_us(void) { return (uint32_t)(now_us() - t0_us); }
uint64_t sys_ticks_us64(void) { return now_us() - t0_us; }
void     sys_delay_us(uint32_t us) { SDL_Delay(us / 1000 + ((us % 1000) ? 1 : 0)); }
void     sys_diag(uint32_t v) { fprintf(stderr, "[diag] 0x%08X\n", v); }

static hal_caps_t caps;
const hal_caps_t *sys_caps(void)
{
	caps.fb_w = W; caps.fb_h = H; caps.fb_bpp = 8;
	caps.main_ram_bytes = 64u << 20;
	caps.cpu_hz = 50000000;
	caps.features = HAL_FEAT_PALETTE | HAL_FEAT_PCM | HAL_FEAT_PAD2
	              | HAL_FEAT_VOICES
	              | HAL_FEAT_PAK | HAL_FEAT_SAVE;   // no FM on the twin...
	if (getenv("RVSTACK_FORCE_FM"))
		caps.features |= HAL_FEAT_FM;   // ...unless testing FM-gated paths
	return &caps;
}

void sys_exit(void) { exit(0); }

// ---------------------------------------------------------------- video
int  fb_width(void)  { return W; }
int  fb_height(void) { return H; }
uint8_t *fb_backbuffer(void) { return fbmem[drawpage]; }

void palette_set(const uint8_t rgb[256][3])
{
	for (int i = 0; i < 256; i++)
		palette[i] = 0xFF000000u | (rgb[i][0] << 16) | (rgb[i][1] << 8)
		           | rgb[i][2];
}

// RVSTACK_SHOT="N:out.bmp[,N2:out2.bmp...]" dumps palette-applied frames —
// proof-of-life screenshots from headless (SDL dummy driver) runs.
static void maybe_shot(void)
{
	static const char *spec;
	static int inited, frame;
	if (!inited) { spec = getenv("RVSTACK_SHOT"); inited = 1; }
	frame++;
	if (!spec)
		return;
	const char *s = spec;
	while (*s) {
		int n = atoi(s);
		const char *c = strchr(s, ':');
		if (!c)
			return;
		const char *e = strchr(c, ',');
		if (n == frame) {
			char path[256];
			size_t len = e ? (size_t)(e - c - 1) : strlen(c + 1);
			if (len > 255) len = 255;
			memcpy(path, c + 1, len); path[len] = 0;
			FILE *fp = fopen(path, "wb");
			if (fp) {
				uint32_t rowsz = W * 3, imgsz = rowsz * H;
				uint8_t hdr[54] = {'B','M'};
				*(uint32_t *)(hdr + 2)  = 54 + imgsz;
				*(uint32_t *)(hdr + 10) = 54;
				*(uint32_t *)(hdr + 14) = 40;
				*(int32_t  *)(hdr + 18) = W;
				*(int32_t  *)(hdr + 22) = H;
				*(uint16_t *)(hdr + 26) = 1;
				*(uint16_t *)(hdr + 28) = 24;
				*(uint32_t *)(hdr + 34) = imgsz;
				fwrite(hdr, 1, 54, fp);
				const uint8_t *src = fbmem[drawpage ^ 1];  // just-presented
				for (int y = H - 1; y >= 0; y--)
					for (int x = 0; x < W; x++) {
						uint32_t c32 = palette[src[y * W + x]];
						uint8_t bgr[3] = { (uint8_t)c32, (uint8_t)(c32 >> 8),
						                   (uint8_t)(c32 >> 16) };
						fwrite(bgr, 1, 3, fp);
					}
				fclose(fp);
				fprintf(stderr, "[shot] frame %d -> %s\n", n, path);
			}
		}
		if (!e)
			return;
		s = e + 1;
	}
}

void fb_present(void)
{
	uint32_t *px;
	int pitch;
	// Headless/software renderers (Xvfb, offscreen) can't lock a texture;
	// skip the on-screen blit but keep page/shot semantics so RVSTACK_SHOT
	// still captures frames without a display.
	if (tex && SDL_LockTexture(tex, NULL, (void **)&px, &pitch) == 0) {
		const uint8_t *src0 = fbmem[drawpage];
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				px[y * (pitch / 4) + x] = palette[src0[y * W + x]];
		SDL_UnlockTexture(tex);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);                      // vsync ~60 Hz
	}
	const uint8_t *src = fbmem[drawpage];
	drawpage ^= 1;
	memcpy(fbmem[drawpage], src, W * H);             // page semantics
	maybe_shot();
}

// ---------------------------------------------------------------- input
static uint32_t pad;

// RVSTACK_INPUT="frame:BTN,frame:BTN,..." injects pad edges headlessly —
// BTN of A B X Y U D L R S(elect) T(start) held for 15 frames from <frame>.
// Finally makes menu-navigation paths reproducible without a window.
static void inject_input(void)
{
	static const char *spec;
	static int inited, frame;
	static uint32_t inj_bit, inj_until;
	if (!inited) { spec = getenv("RVSTACK_INPUT"); inited = 1; }
	frame++;
	if (!spec)
		return;
	if (inj_until && (uint32_t)frame >= inj_until) {
		pad &= ~inj_bit;
		inj_until = 0;
	}
	const char *s = spec;
	while (*s) {
		int n = atoi(s);
		const char *c = strchr(s, ':');
		if (!c)
			return;
		if (n == frame) {
			uint32_t bit = 0;
			switch (c[1]) {
			case 'A': bit = HAL_BTN_A; break;
			case 'B': bit = HAL_BTN_B; break;
			case 'X': bit = HAL_BTN_X; break;
			case 'Y': bit = HAL_BTN_Y; break;
			case 'U': bit = HAL_BTN_UP; break;
			case 'D': bit = HAL_BTN_DOWN; break;
			case 'L': bit = HAL_BTN_LEFT; break;
			case 'R': bit = HAL_BTN_RIGHT; break;
			case 'S': bit = HAL_BTN_SELECT; break;
			case 'T': bit = HAL_BTN_START; break;
			}
			pad |= bit;
			inj_bit = bit;
			inj_until = (uint32_t)frame + 15;
			fprintf(stderr, "[inject] frame %d: %c\n", n, c[1]);
		}
		const char *e = strchr(c, ',');
		if (!e)
			return;
		s = e + 1;
	}
}

void input_poll(void)
{
	inject_input();
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT)
			exit(0);
		if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP)
			continue;
		uint32_t bit = 0;
		switch (e.key.keysym.sym) {
		case SDLK_UP: bit = HAL_BTN_UP; break;
		case SDLK_DOWN: bit = HAL_BTN_DOWN; break;
		case SDLK_LEFT: bit = HAL_BTN_LEFT; break;
		case SDLK_RIGHT: bit = HAL_BTN_RIGHT; break;
		case SDLK_z: bit = HAL_BTN_A; break;
		case SDLK_x: bit = HAL_BTN_B; break;
		case SDLK_a: bit = HAL_BTN_X; break;
		case SDLK_s: bit = HAL_BTN_Y; break;
		case SDLK_q: bit = HAL_BTN_L1; break;
		case SDLK_w: bit = HAL_BTN_R1; break;
		case SDLK_TAB: bit = HAL_BTN_SELECT; break;
		case SDLK_RETURN: bit = HAL_BTN_START; break;
		case SDLK_ESCAPE: exit(0);
		}
		if (bit) {
			if (e.type == SDL_KEYDOWN) pad |= bit;
			else                       pad &= ~bit;
		}
	}
}

uint32_t input_buttons(int player) { return player ? 0 : pad; }

void input_state(int player, hal_pad_t *out)
{
	memset(out, 0, sizeof(*out));
	out->buttons = (uint16_t)input_buttons(player);
}

// ---------------------------------------------------------------- audio
static SDL_AudioDeviceID adev;

int audio_stream_open(int rate)
{
	if (rate != 48000)
		return -1;
	if (!adev) {
		SDL_AudioSpec want = {0};
		want.freq = 48000; want.format = AUDIO_S16SYS;
		want.channels = 2; want.samples = 512;
		adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
		SDL_PauseAudioDevice(adev, 0);
	}
	return 0;
}

// The console's FIFO is a fixed depth that drains at exactly 48 kHz, and
// callers (SDL_lite_audio_pump) loop "top the FIFO up until it is full".
// Deriving the fill level from the HOST queue breaks that loop whenever the
// host sink is not a real 48 kHz clock: the `dummy`/`disk` drivers never
// drain (pump blocks forever) and a null ALSA PCM drains instantly (the
// fill-until-full loop NEVER terminates, so the game never presents a frame
// again). Both made every audio-dependent code path untestable headlessly.
// So model the FIFO on the wall clock instead — identical behaviour with or
// without a real audio device. The host queue still gets the samples so that
// a real device actually plays them.
#define AUD_FIFO_CAP (2048 - 8)             // mirror the console FIFO depth

static int      aud_level;                  // frames currently "in" the FIFO
static uint32_t aud_last_us;                // last drain timestamp
static uint32_t aud_frac_us;                // sub-frame remainder, avoids drift

static void aud_drain(void)
{
	uint32_t now = sys_ticks_us();
	if (!aud_last_us) { aud_last_us = now ? now : 1; return; }
	uint32_t dt = now - aud_last_us + aud_frac_us;
	uint32_t frames = (uint32_t)(((uint64_t)dt * 48000) / 1000000);
	aud_frac_us = dt - (uint32_t)(((uint64_t)frames * 1000000) / 48000);
	aud_last_us = now;
	aud_level -= (int)frames;
	if (aud_level < 0)
		aud_level = 0;
}

int audio_stream_free(void)
{
	if (!adev)
		audio_stream_open(48000);
	aud_drain();
	return aud_level >= AUD_FIFO_CAP ? 0 : AUD_FIFO_CAP - aud_level;
}

static void vmx_mix_frame(int32_t *accl, int32_t *accr);   // twin mixer, below
static uint8_t vmx_master_get(void);
static int vmx_any_active(void);

int audio_stream_write(const int16_t *pcm, int nframes)
{
	if (!adev)
		audio_stream_open(48000);
	aud_drain();
	aud_level += nframes;                       // console FIFO accounting
	// The CONSOLE sums the vmx voices into every 48 kHz frame regardless of
	// where the frame came from (CPU stream or audio_pump). Mirror that here:
	// each pushed frame gets the hardware-law vmx mix added (master applies to
	// the vmx sum only, exactly like the SoC).
	if (vmx_any_active()) {
		static int16_t mixed[1024 * 2];
		for (int off = 0; off < nframes; off += 1024) {
			int n = nframes - off > 1024 ? 1024 : nframes - off;
			for (int i = 0; i < n; i++) {
				int32_t vl = 0, vr = 0;
				vmx_mix_frame(&vl, &vr);
				int32_t l = pcm[2*(off+i)]   + ((vl * vmx_master_get()) >> 8);
				int32_t r = pcm[2*(off+i)+1] + ((vr * vmx_master_get()) >> 8);
				if (l > 32767) l = 32767; if (l < -32768) l = -32768;
				if (r > 32767) r = 32767; if (r < -32768) r = -32768;
				mixed[2*i] = (int16_t)l; mixed[2*i+1] = (int16_t)r;
			}
			if (SDL_GetQueuedAudioSize(adev) < 48000 / 4 * 4)
				SDL_QueueAudio(adev, mixed, (Uint32)n * 4);
		}
		return nframes;
	}
	// Keep the host device fed, but never block on it: with no real sink the
	// queue would grow without bound, and blocking here is what deadlocked
	// the headless pump.
	if (SDL_GetQueuedAudioSize(adev) < 48000 / 4 * 4)
		SDL_QueueAudio(adev, pcm, (Uint32)nframes * 4);
	return nframes;
}

#define PCM_VOICES 4
static struct { const int16_t *pcm; uint32_t pos, step, len; } voice[PCM_VOICES];

int pcm_play(int ch, const int16_t *pcm, int nsamples, int rate)
{
	if (ch < 0)
		for (ch = 0; ch < PCM_VOICES && voice[ch].pcm; ch++) ;
	if (ch >= PCM_VOICES)
		return -1;
	voice[ch].pcm  = pcm;
	voice[ch].pos  = 0;
	voice[ch].len  = (uint32_t)nsamples << 16;
	voice[ch].step = ((uint64_t)rate << 16) / 48000;
	return ch;
}

void audio_pump(void)
{
	int16_t mix[800 * 2];
	int n = 48000 / 60;
	for (int i = 0; i < n; i++) {
		int32_t s = 0;
		for (int v = 0; v < PCM_VOICES; v++) {
			if (!voice[v].pcm)
				continue;
			s += voice[v].pcm[voice[v].pos >> 16];
			voice[v].pos += voice[v].step;
			if (voice[v].pos >= voice[v].len)
				voice[v].pcm = NULL;
		}
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		mix[2 * i] = mix[2 * i + 1] = (int16_t)s;
	}
	audio_stream_write(mix, n);                 // vmx voices join here
}

// --- vmx software twin: the hardware mixer's integer law, per-frame ---------
// (soc/voice_mixer/README.md; keep bit-compatible with gen_ref.py)
#define VMX_NVOICES  32
#define VMX_PCM_BASE 28
static struct {
	vmx_sample_t s;
	uint64_t pos;                       // 24.16
	uint32_t step;
	uint8_t  vol, pan, active;
} vmx[VMX_NVOICES];
static uint8_t vmx_master_vol = 0xFF;

int  vmx_voices(void) { return VMX_NVOICES; }
void vmx_sample_flush(const vmx_sample_t *s) { (void)s; }   // coherent on PC

static int16_t vmx_fetch(const vmx_sample_t *s, uint32_t idx)
{
	switch (s->format) {
	case VMX_FMT_S16: return ((const int16_t *)s->data)[idx];
	case VMX_FMT_U8:  return (int16_t)((((const uint8_t *)s->data)[idx] - 128) << 8);
	default:          return (int16_t)(((const int8_t  *)s->data)[idx] << 8);
	}
}

int vmx_key_on(int voice, const vmx_sample_t *smp, uint32_t step,
               uint8_t vol, uint8_t pan)
{
	if (voice < 0) {
		for (voice = 0; voice < VMX_PCM_BASE && vmx[voice].active; voice++) ;
		if (voice == VMX_PCM_BASE) return -1;
	}
	if (voice >= VMX_NVOICES) return -1;
	vmx[voice].s = *smp; vmx[voice].pos = 0; vmx[voice].step = step;
	vmx[voice].vol = vol; vmx[voice].pan = pan; vmx[voice].active = 1;
	return voice;
}
void vmx_set(int voice, uint32_t step, uint8_t vol, uint8_t pan)
{
	vmx[voice].step = step; vmx[voice].vol = vol; vmx[voice].pan = pan;
}
void vmx_key_off(int voice, int to_loop_end)
{
	if (to_loop_end) vmx[voice].s.loop = VMX_LOOP_NONE;
	else             vmx[voice].active = 0;
}
uint32_t vmx_active_mask(void)
{
	uint32_t m = 0;
	for (int v = 0; v < VMX_NVOICES; v++) if (vmx[v].active) m |= 1u << v;
	return m;
}
uint32_t vmx_pos(int voice) { return (uint32_t)(vmx[voice].pos >> 16); }
void vmx_master(uint8_t vol) { vmx_master_vol = vol; }
static uint8_t vmx_master_get(void) { return vmx_master_vol; }
static int vmx_any_active(void)
{
	for (int v = 0; v < VMX_NVOICES; v++) if (vmx[v].active) return 1;
	return 0;
}

// one output frame of the hardware law (called from audio_pump's loop)
static void vmx_mix_frame(int32_t *accl, int32_t *accr)
{
	for (int v = 0; v < VMX_NVOICES; v++) {
		if (!vmx[v].active) continue;
		const vmx_sample_t *sp = &vmx[v].s;
		uint32_t idx0 = (uint32_t)(vmx[v].pos >> 16);
		uint32_t frac = vmx[v].pos & 0xFFFF;
		uint32_t idx1 = idx0 + 1;
		if (sp->loop == VMX_LOOP_FWD) { if (idx1 >= sp->loop_end) idx1 = sp->loop_start; }
		else if (idx1 >= sp->frames)  idx1 = sp->frames - 1;
		int32_t s0 = vmx_fetch(sp, idx0), s1 = vmx_fetch(sp, idx1);
		int32_t smp = s0 + (((s1 - s0) * (int32_t)frac) >> 16);
		int32_t sv  = (smp * vmx[v].vol) >> 8;
		*accl += (sv * (255 - vmx[v].pan)) >> 8;
		*accr += (sv * vmx[v].pan) >> 8;
		vmx[v].pos += vmx[v].step;
		if (sp->loop == VMX_LOOP_FWD) {
			if ((vmx[v].pos >> 16) >= sp->loop_end)
				vmx[v].pos -= (uint64_t)(sp->loop_end - sp->loop_start) << 16;
		} else if ((vmx[v].pos >> 16) >= sp->frames)
			vmx[v].active = 0;
	}
}

void opl_write(uint16_t reg, uint8_t val)
{
	// RVSTACK_OPLLOG=file captures the FM register stream — proof the music
	// engine emits sensible writes without needing the hardware chip.
	static FILE *log; static int inited;
	if (!inited) {
		const char *p = getenv("RVSTACK_OPLLOG");
		if (p) log = fopen(p, "w");
		inited = 1;
	}
	if (log)
		fprintf(log, "%03X %02X\n", reg, val);
}

// ---------------------------------------------------------------- pak
static uint8_t *pak_mem;

int pak_open(const char *name, pak_file_t *out)
{
	(void)name;
	const char *path = getenv("RVSTACK_PAK");
	if (!path)
		path = "game.pak";
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	free(pak_mem);
	pak_mem = malloc((size_t)sz);
	if (fread(pak_mem, 1, (size_t)sz, f) != (size_t)sz) {
		fclose(f);
		return -1;
	}
	fclose(f);
	out->base = (uintptr_t)pak_mem;
	out->size = (uint32_t)(sz > 2 ? sz - 2 : 0);   // EOF-wedge parity
	out->pos  = 0;
	return 0;
}

int pak_open_at(uint32_t dst_off, pak_file_t *out)
{
	(void)dst_off;                              // PC: no fixed windows
	return pak_open(NULL, out);
}

/* Auto-load-by-name (console: host openfile + slot DMA into raw main_ram
 * offsets). The PC twin has no main_ram window to land bytes in — callers
 * dereference 0x4000_0000-based addresses directly — so these decline (<0)
 * and callers take their documented fallback (pak_open_at -> $RVSTACK_PAK),
 * which exercises the same downstream parsing. */
int pak_bind_named(const char *name)
{
	(void)name;
	return -1;
}

int pak_slot_read(uint32_t dst_off, uint32_t slot_off, uint32_t nbytes)
{
	(void)dst_off; (void)slot_off; (void)nbytes;
	return -1;
}

int pak_open_named(const char *name, uint32_t dst_off, pak_file_t *out)
{
	(void)name; (void)dst_off; (void)out;
	return -1;
}

int pak_read(pak_file_t *f, void *dst, int nbytes)
{
	uint32_t left = f->size - f->pos;
	if ((uint32_t)nbytes > left)
		nbytes = (int)left;
	memcpy(dst, (const void *)(uintptr_t)(f->base + f->pos), (size_t)nbytes);
	f->pos += (uint32_t)nbytes;
	return nbytes;
}

int pak_seek(pak_file_t *f, int offset, int whence)
{
	long p = (whence == 0) ? offset
	       : (whence == 1) ? (long)f->pos + offset : (long)f->size + offset;
	if (p < 0 || p > (long)f->size)
		return -1;
	f->pos = (uint32_t)p;
	return 0;
}

// ---------------------------------------------------------------- saves
int save_open(const char *name, uint32_t size, save_file_t *f)
{
	size = (size + 3) & ~3u;
	uint8_t *mem = calloc(1, size);
	snprintf(f->_path, sizeof(f->_path), "%s.sav", name);
	f->base = (uintptr_t)mem;
	f->size = size;
	FILE *fp = fopen(f->_path, "rb");
	if (!fp)
		return 1;                                   // created
	size_t got = fread(mem, 1, size, fp);
	(void)got;
	fclose(fp);
	return 0;                                       // opened
}

int save_commit(save_file_t *f)
{
	FILE *fp = fopen(f->_path, "wb");
	if (!fp)
		return -1;
	fwrite((const void *)(uintptr_t)f->base, 1, f->size, fp);
	fclose(fp);
	return 0;
}

int blit(void *dst, const void *src, uint32_t w, uint32_t h,
         uint32_t ss, uint32_t ds)
{
	(void)dst; (void)src; (void)w; (void)h; (void)ss; (void)ds;
	return -1;                          // PC: memcpy path is plenty
}
void blit_wait(void) {}
int blit_ck(void *dst, const void *src, uint32_t w, uint32_t h,
            uint32_t ss, uint32_t ds)
{ (void)dst; (void)src; (void)w; (void)h; (void)ss; (void)ds; return -1; }
void fb_present_dma(void) { fb_present(); }
void fb_flip_poll(void) {}              // PC presents immediately: no deferral

uint32_t save_last_hw_err(void) { return 0; }
uint32_t save_restore_code(void) { return 0; }  // PC restores synchronously
int save_diag_getfile(uint16_t s, uint8_t *b, int n) { (void)s; memset(b, 0, (size_t)n); return 0; }
int save_diag_openfile_raw(uint16_t s) { (void)s; return 0; }
