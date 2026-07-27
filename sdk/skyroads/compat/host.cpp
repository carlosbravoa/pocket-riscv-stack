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

void present(const renderer::RenderedFrame& rf)
{
	uint8_t* fb = fb_backbuffer();
	const int w = fb_width();
	std::memcpy(fb + LETTERBOX_Y * w, rf.frame.pixels.data(), 320u * 200u);
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

	uint64_t next_step = sys_ticks_us64();
	bool first_frame = true;
	bool was_gameplay = false;

	for (;;) {
		input_poll();  // latch the pad once per loop (hal.h contract)
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

		audio_rv.advance(now);
		present(ren.render_scene(tick.render_scene));
		if (first_frame) {
			sys_diag(0xBEAC0007);
			first_frame = false;
		}
	}
}
