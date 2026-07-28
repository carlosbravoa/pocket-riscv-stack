// Skyroads-c on riscv-stack: the console host.
//
// Replaces src/platform/main.cpp (SDL2) with the hal.h-direct pattern (Doom).
// The game core is untouched: AttractModeApp ticks at the DOS design rate of
// 36 Hz (see upstream main.cpp on the PIT/180.02 Hz provenance), the renderer
// produces a 320x200 indexed frame + a 256-entry palette per frame, and this
// file is only clocking, input latching, present, audio dispatch and saves.
//
// Presentation: 320x200 letterboxed onto the 320x240 panel (rows 20..219),
// palette_set() right after fb_present() so palette fades land glitch-free
// (the DOS game fades by DAC reprogramming — our CLUT is the same machine).
//
// Input map (PORTING.md): dpad = menus/throttle/steer, A = jump / skip intro,
// B = back (Esc), START = select / restart, SELECT+START = quit to picker
// (progress is persisted first). Edges are STICKY until a 36 Hz step consumes
// them — a tap between steps must not vanish (upstream main.cpp, same fix).
//
// Boot beacons (sys_diag, watched by the RVSTACK_SKYROADS sim scenario):
//   0xBEAC0001 entry   0xBEAC0002 pak mounted   0xBEAC0003 levels parsed
//   0xBEAC0004 assets + renderer + audio ready  0xBEAC0007 first frame shown
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "audio_rv.hpp"
#include "core/core.hpp"
#include "data/config.hpp"
#include "data/data.hpp"
#include "pak_assets.hpp"
#include "renderer/renderer.hpp"

extern "C" {
#include "hal.h"
}

namespace {

using namespace skyroads;
using rvstack::asset_bytes;

constexpr uint64_t STEP_US = 1000000 / 36;  // the DOS design rate
constexpr int MAX_CATCH_UP_STEPS = 4;
constexpr int LETTERBOX_Y = (240 - 200) / 2;

renderer::AttractModeAssets load_render_assets()
{
	renderer::AttractModeAssets a;
	char world[16] = "worldN.lzs";
	for (int i = 0; i <= 9; ++i) {
		world[5] = (char)('0' + i);
		a.worlds.push_back(data::load_image_archive_bytes(asset_bytes(world)));
	}
	a.intro = data::load_image_archive_bytes(asset_bytes("intro.lzs"));
	a.anim = data::load_image_archive_bytes(asset_bytes("anim.lzs"));
	a.main_menu = data::load_image_archive_bytes(asset_bytes("mainmenu.lzs"));
	a.help_menu = data::load_image_archive_bytes(asset_bytes("helpmenu.lzs"));
	a.settings_menu = data::load_image_archive_bytes(asset_bytes("setmenu.lzs"));
	a.go_menu = data::load_image_archive_bytes(asset_bytes("gomenu.lzs"));
	a.cars = data::load_image_archive_bytes(asset_bytes("cars.lzs"));
	a.dashboard = data::load_image_archive_bytes(asset_bytes("dashbrd.lzs"));
	a.trekdat = data::load_trekdat_lzs_bytes(asset_bytes("trekdat.lzs"));
	a.oxygen_gauge = data::load_dashboard_dat_bytes(asset_bytes("oxy_disp.dat"));
	a.fuel_gauge = data::load_dashboard_dat_bytes(asset_bytes("ful_disp.dat"));
	a.speed_gauge = data::load_dashboard_dat_bytes(asset_bytes("speed.dat"));
	return a;
}

audio::AttractAudioAssets load_audio_assets()
{
	audio::AttractAudioAssets a;
	a.intro = data::load_intro_snd_bytes(asset_bytes("intro.snd"));
	a.sfx = data::load_sfx_snd_bytes(asset_bytes("sfx.snd"));
	a.muzax = data::load_muzax_lzs_bytes(asset_bytes("muzax.lzs"));
	return a;
}

// Sticky-edge pad latching: a press shorter than one 36 Hz step, or one that
// lands on a loop iteration that does not step, must still register exactly
// once. Same contract as upstream's KeyLatch, buttons instead of scancodes.
struct PadLatch {
	uint32_t prev = 0;
	core::AppInput pending{};

	void fold(uint32_t held)
	{
		uint32_t edges = held & ~prev;
		prev = held;
		if (edges & HAL_BTN_UP) pending.up = true;
		if (edges & HAL_BTN_DOWN) pending.down = true;
		if (edges & HAL_BTN_LEFT) pending.left = true;
		if (edges & HAL_BTN_RIGHT) pending.right = true;
		if (edges & HAL_BTN_START) pending.enter = true;
		if (edges & HAL_BTN_B) pending.escape = true;
		if (edges & HAL_BTN_A) pending.space = true;
	}

	core::AppInput sample(uint32_t held) const
	{
		core::AppInput in = pending;
		in.up_held = (held & HAL_BTN_UP) != 0;
		in.down_held = (held & HAL_BTN_DOWN) != 0;
		in.left_held = (held & HAL_BTN_LEFT) != 0;
		in.right_held = (held & HAL_BTN_RIGHT) != 0;
		in.enter_held = (held & HAL_BTN_START) != 0;
		in.space_held = (held & HAL_BTN_A) != 0;
		return in;
	}

	void clear() { pending = core::AppInput{}; }
};

// Progress in the original's own 66-byte skyroads.cfg block (checksum word +
// 2 setting words + 30 completion words), through the Pocket save mechanism.
save_file_t g_save;
bool g_save_ok = false;

data::GameConfig restore_config()
{
	int r = save_open("skyroads.cfg", data::CONFIG_BYTES, &g_save);
	g_save_ok = (r >= 0);
	if (r != 0)  // created fresh, or no save access: zeroed = first run
		return data::GameConfig{};
	data::Bytes raw(reinterpret_cast<const uint8_t*>(g_save.base),
	                reinterpret_cast<const uint8_t*>(g_save.base) + g_save.size);
	return data::load_game_config_bytes(raw);
}

void persist_config(core::AttractModeApp& app, data::GameConfig& cfg)
{
	bool changed = false;
	const auto& counts = app.road_completions();
	for (std::size_t i = 0; i < counts.size(); ++i)
		if (cfg.road_completions[i] != counts[i]) {
			cfg.road_completions[i] = counts[i];
			changed = true;
		}
	if (cfg.setting_a != app.input_device()) {
		cfg.setting_a = (uint16_t)app.input_device();
		changed = true;
	}
	if (cfg.setting_b != app.sound_option()) {
		cfg.setting_b = (uint16_t)app.sound_option();
		changed = true;
	}
	if (!changed || !g_save_ok)
		return;
	data::Bytes raw = data::game_config_to_bytes(cfg);
	std::memcpy(reinterpret_cast<void*>(g_save.base), raw.data(),
	            raw.size() < g_save.size ? raw.size() : g_save.size);
	save_commit(&g_save);
}

#ifdef SKY_TIMING
// Stage timers for the hardware-profiling build (`make TIMING=1`): drawn as
// text in the top letterbox bar, which the 200-line game image never touches.
// T = app.tick total, R = render_scene, A = audio advance, L = full loop —
// all in microseconds for the LAST loop iteration.
uint32_t g_t_tick, g_t_render, g_t_audio, g_t_loop;

#include "font8x8_basic.h"

void dbg_text(uint8_t* fb, int w, int x, int y, const char* s)
{
	for (int ci = 0; s[ci]; ci++) {
		const char* g = font8x8_basic[(uint8_t)s[ci] & 0x7F];
		for (int ry = 0; ry < 8; ry++)
			for (int rx = 0; rx < 8; rx++)
				fb[(y + ry) * w + x + ci * 8 + rx] =
				    ((g[ry] >> rx) & 1) ? 0x63 : 0x00;
	}
}
#endif

void present(const renderer::RenderedFrame& rf)
{
	uint8_t* fb = fb_backbuffer();
	const int w = fb_width();
	std::memcpy(fb + LETTERBOX_Y * w, rf.frame.pixels.data(), 320u * 200u);
#ifdef SKY_TIMING
	char buf[48];
	std::snprintf(buf, sizeof buf, "T%6lu R%6lu A%6lu L%7lu",
	              (unsigned long)g_t_tick, (unsigned long)g_t_render,
	              (unsigned long)g_t_audio, (unsigned long)g_t_loop);
	dbg_text(fb, w, 4, 6, buf);
#endif
	fb_present();
	// Right after the flip: glitch-free even mid-fade (hal.h contract).
	uint8_t rgb[256][3];
	for (int i = 0; i < 256; ++i) {
		rgb[i][0] = rf.palette.colors[i].r;
		rgb[i][1] = rf.palette.colors[i].g;
		rgb[i][2] = rf.palette.colors[i].b;
	}
	palette_set(rgb);
}

} // namespace

#ifndef RVSTACK_PC
extern "C" void rvstack_eh_init(void);
#endif

int main()
{
	sys_init();
#ifndef RVSTACK_PC
	rvstack_eh_init();  // C++ throws below (data loaders); see game.ld .eh_frame
#endif
	sys_diag(0xBEAC0001);

	if (rvstack::assets_mount() != 0) {
		sys_diag(0xDEAD9A99);
		for (;;)
			;
	}
	sys_diag(0xBEAC0002);

	auto levels = data::levels_from_roads_archive(
	    data::load_roads_lzs_bytes(asset_bytes("roads.lzs")));
	auto demo = data::load_demo_rec_bytes(asset_bytes("demo.rec"));
	sys_diag(0xBEAC0003);

	renderer::ReferenceRenderer ren(load_render_assets());
	// Warm the TREKDAT shape memo NOW: shape_ref() decodes lazily, so with a
	// cold cache the first sight of new geometry costs a parse burst mid-
	// gameplay — a frame drop exactly when a new block pattern scrolls in.
	// Walking every pointer chain here moves all of it into the boot screen
	// (the memo makes this O(total shapes): chains overlap and terminate at
	// the stream end).
	for (const auto& rec : ren.assets().trekdat.records) {
		for (uint16_t start : rec.pointer_table) {
			std::optional<uint16_t> off = start;
			while (off && rec.shape_ref(*off))
				off = rec.next_shape_offset_fast(*off);
		}
	}
	rvstack::AudioRv audio_rv(load_audio_assets());
	core::AttractModeApp app(std::move(levels), demo);
	{
		std::size_t groups = 0;
		for (const auto& g : ren.assets().anim.frames)
			if (!g.empty()) groups += 1;
		app.set_intro_anim_group_count(groups);
	}
	data::GameConfig cfg = restore_config();
	app.set_road_completions(cfg.road_completions);
	app.set_settings(cfg.setting_a, cfg.setting_b);
	sys_diag(0xBEAC0004);

	// The 200-line frame never touches the letterbox bars: clear both
	// backbuffers once and they stay black.
	for (int i = 0; i < 2; ++i) {
		std::memset(fb_backbuffer(), 0, (size_t)fb_width() * fb_height());
		fb_present();
	}

	PadLatch pad;
	core::AppTickResult tick = app.tick(core::AppInput{});
	audio_rv.apply_commands(tick.audio_commands);

	// Persistent frame + play cache: play scenes render incrementally at the
	// DOS write budget (see PlaySceneCache); menus take the pure path.
	renderer::RenderedFrame frame;
	renderer::PlaySceneCache play_cache;
#ifdef RVSTACK_PC
	// SKY_VERIFY=1 (twin only): render every play frame BOTH ways and compare
	// — the proof the incremental path is byte-identical to the pure one.
	const bool verify = getenv("SKY_VERIFY") != nullptr;
	unsigned long verify_frames = 0, verify_bad = 0;
#endif

	uint64_t next_step = sys_ticks_us64();
	bool first_frame = true;
	bool was_gameplay = false;

	uint64_t last_poll = 0;
	for (;;) {
		// The skip-render path makes this loop spin far faster than the
		// frame rate; pace input_poll() to ~120 Hz — the hal.h contract is
		// once per frame, and short taps are still double-covered.
		const uint64_t poll_now = sys_ticks_us64();
		if (poll_now - last_poll >= 8000) {
			input_poll();
			last_poll = poll_now;
		}
		const uint32_t held = input_buttons(0);
		pad.fold(held);

		if ((held & HAL_BTN_SELECT) && (held & HAL_BTN_START)) {
			persist_config(app, cfg);
			sys_exit();
		}

		const uint64_t now = sys_ticks_us64();
		int steps = 0;
		while (now >= next_step && steps < MAX_CATCH_UP_STEPS) {
			tick = app.tick(pad.sample(input_buttons(0)));
			pad.clear();
			audio_rv.apply_commands(tick.audio_commands);
			next_step += STEP_US;
			steps += 1;
		}
		if (steps == MAX_CATCH_UP_STEPS && now >= next_step)
			next_step = now;  // hopelessly behind: drop the backlog

		// Leaving gameplay = a road was finished or abandoned: persist.
		const bool in_gameplay = (tick.mode == core::AppMode::Gameplay);
		if (was_gameplay && !in_gameplay)
			persist_config(app, cfg);
		was_gameplay = in_gameplay;

#ifdef SKY_TIMING
		const uint64_t t_after_tick = sys_ticks_us64();
		g_t_tick = (uint32_t)(t_after_tick - now);
		audio_rv.advance(now);
		g_t_audio = (uint32_t)(sys_ticks_us64() - t_after_tick);
#else
		audio_rv.advance(now);
#endif

		// Re-render only when the sim stepped — the scene cannot change
		// otherwise. Between presents the loop spins on input_poll and the
		// music sequencer, which keeps the 180 Hz ticks fine-grained.
		if (steps == 0 && !first_frame)
			continue;

#ifdef SKY_TIMING
		static uint64_t last_present;
		g_t_loop = last_present ? (uint32_t)(now - last_present) : 0;
		last_present = now;
		const uint64_t t_before_render = sys_ticks_us64();
#endif
		const bool play =
		    tick.render_scene.tag == core::RenderScene::Tag::DemoPlayback ||
		    tick.render_scene.tag == core::RenderScene::Tag::Gameplay;
		if (play) {
			ren.render_play_scene_incremental(frame, play_cache,
			                                  tick.render_scene.play);
#ifdef RVSTACK_PC
			if (verify) {
				renderer::RenderedFrame pure =
				    ren.render_scene(tick.render_scene);
				verify_frames++;
				if (pure.frame.pixels != frame.frame.pixels ||
				    std::memcmp(pure.palette.colors.data(),
				                frame.palette.colors.data(),
				                sizeof(pure.palette.colors)) != 0) {
					verify_bad++;
					int bx0 = 320, by0 = 200, bx1 = -1, by1 = -1;
					for (int py = 0; py < 200; ++py)
						for (int px = 0; px < 320; ++px)
							if (pure.frame.pixels[py * 320 + px] !=
							    frame.frame.pixels[py * 320 + px]) {
								if (px < bx0) bx0 = px;
								if (px > bx1) bx1 = px;
								if (py < by0) by0 = py;
								if (py > by1) by1 = py;
							}
					std::fprintf(stderr,
					             "[SKY_VERIFY] MISMATCH frame %lu (%lu bad) "
					             "diff box x%d..%d y%d..%d mode%d win%d ship%d\n",
					             verify_frames, verify_bad, bx0, bx1, by0, by1,
					             (int)tick.render_scene.play.craft_state,
					             (int)tick.render_scene.play.did_win,
					             (int)tick.render_scene.play.ship.state);
				}
				if (verify_frames % 500 == 0)
					std::fprintf(stderr, "[SKY_VERIFY] %lu frames, %lu bad\n",
					             verify_frames, verify_bad);
			}
#endif
		} else {
			frame = ren.render_scene(tick.render_scene);
			play_cache.frame_primed = false;  // frame no longer holds play state
		}
#ifdef SKY_TIMING
		g_t_render = (uint32_t)(sys_ticks_us64() - t_before_render);
#endif
		present(frame);
		if (first_frame) {
			sys_diag(0xBEAC0007);
			first_frame = false;
		}
	}
}
