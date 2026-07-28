// Edge-state fuzz harness (PC-only, built ad hoc with sanitizers): seeds the
// gameplay session into extreme fall-off states — the hardware freeze report
// is "fell off at the far right of the screen, amber trap bars" — then steps
// physics and renders every frame through BOTH render paths. Meant to make
// the rv32 trap reproduce as an ASan/UBSan report with a symbolized stack.
//
// Build/run: see the session notes; not part of any shipped target.
#include <cstdio>
#include <cstring>

#include "audio_rv.hpp"
#include "core/core.hpp"
#include "data/data.hpp"
#include "pak_assets.hpp"
#include "renderer/renderer.hpp"

extern "C" {
#include "hal.h"
}

using namespace skyroads;

int main()
{
	sys_init();
	if (rvstack::assets_mount() != 0) {
		std::fprintf(stderr, "no pak\n");
		return 1;
	}
	auto levels = data::levels_from_roads_archive(
	    data::load_roads_lzs_bytes(rvstack::asset_bytes("roads.lzs")));
	auto demo = data::load_demo_rec_bytes(rvstack::asset_bytes("demo.rec"));

	renderer::AttractModeAssets rassets;
	{
		char world[16] = "worldN.lzs";
		for (int i = 0; i <= 9; ++i) {
			world[5] = (char)('0' + i);
			rassets.worlds.push_back(
			    data::load_image_archive_bytes(rvstack::asset_bytes(world)));
		}
		rassets.intro = data::load_image_archive_bytes(rvstack::asset_bytes("intro.lzs"));
		rassets.anim = data::load_image_archive_bytes(rvstack::asset_bytes("anim.lzs"));
		rassets.main_menu =
		    data::load_image_archive_bytes(rvstack::asset_bytes("mainmenu.lzs"));
		rassets.help_menu =
		    data::load_image_archive_bytes(rvstack::asset_bytes("helpmenu.lzs"));
		rassets.settings_menu =
		    data::load_image_archive_bytes(rvstack::asset_bytes("setmenu.lzs"));
		rassets.go_menu = data::load_image_archive_bytes(rvstack::asset_bytes("gomenu.lzs"));
		rassets.cars = data::load_image_archive_bytes(rvstack::asset_bytes("cars.lzs"));
		rassets.dashboard =
		    data::load_image_archive_bytes(rvstack::asset_bytes("dashbrd.lzs"));
		rassets.trekdat = data::load_trekdat_lzs_bytes(rvstack::asset_bytes("trekdat.lzs"));
		rassets.oxygen_gauge =
		    data::load_dashboard_dat_bytes(rvstack::asset_bytes("oxy_disp.dat"));
		rassets.fuel_gauge =
		    data::load_dashboard_dat_bytes(rvstack::asset_bytes("ful_disp.dat"));
		rassets.speed_gauge =
		    data::load_dashboard_dat_bytes(rvstack::asset_bytes("speed.dat"));
	}
	renderer::ReferenceRenderer ren(std::move(rassets));

	core::AttractModeApp app(std::move(levels), demo);
	// Walk Intro -> MainMenu -> GoMenu -> Gameplay with enter edges.
	core::AppInput enter{};
	enter.enter = true;
	for (int i = 0; i < 200 && app.mode() != core::AppMode::Gameplay; ++i)
		app.tick(app.mode() == core::AppMode::Intro || i % 4 == 0 ? enter
		                                                          : core::AppInput{});
	if (app.mode() != core::AppMode::Gameplay) {
		std::fprintf(stderr, "never reached gameplay (mode %d)\n", (int)app.mode());
		return 1;
	}

	renderer::RenderedFrame frame;
	renderer::PlaySceneCache cache;
	unsigned long rendered = 0;

	core::AppInput right{};
	right.right_held = true;
	right.up_held = true;

	// Sweep: seed the ship at/beyond both road edges, at several heights and
	// forward positions and lateral speeds, in Alive and Fallen states, then
	// run several physics ticks rendering each frame both ways.
	const double xs[] = {-600, -80, 0, 60, 90, 400, 470, 500, 512, 540, 600, 1200};
	const double ys[] = {80, 60, 20, 0, -40, -200, -2000};
	const double zs[] = {0.2, 4.0, 20.0, 80.0, 150.0};
	const double xvel[] = {-4.0, 0.0, 4.0};
	for (double x : xs)
		for (double y : ys)
			for (double z : zs)
				for (double xv : xvel)
					for (int st = 0; st < 2; ++st) {
						core::GameplaySession& s = app.gameplay_session();
						s.ship.x_position = x;
						s.ship.y_position = y;
						s.ship.z_position = z;
						s.ship.x_movement_base = xv;
						s.ship.y_velocity = (y < 80) ? -3.0 : 0.0;
						s.ship.is_on_ground = false;
						s.ship.state =
						    st ? core::ShipState::Fallen : core::ShipState::Alive;
						s.expected_ship = s.ship;
						for (int t = 0; t < 8; ++t) {
							core::AppTickResult r = app.tick(right);
							if (r.render_scene.tag !=
							        core::RenderScene::Tag::Gameplay &&
							    r.render_scene.tag !=
							        core::RenderScene::Tag::DemoPlayback)
								break;  // death animation ended -> menu
							ren.render_play_scene_incremental(
							    frame, cache, r.render_scene.play);
							renderer::RenderedFrame pure =
							    ren.render_scene(r.render_scene);
							rendered++;
							if (pure.frame.pixels != frame.frame.pixels)
								std::fprintf(stderr,
								             "PIXDIFF x=%g y=%g z=%g xv=%g st=%d t=%d\n",
								             x, y, z, xv, st, t);
						}
						// If the session died, walk back into gameplay.
						for (int i = 0; i < 400 && app.mode() != core::AppMode::Gameplay;
						     ++i)
							app.tick(i % 4 == 0 ? enter : core::AppInput{});
						if (app.mode() != core::AppMode::Gameplay) {
							std::fprintf(stderr, "lost gameplay mode\n");
							return 1;
						}
					}
	std::printf("edgetest done: %lu frames rendered, no faults\n", rendered);
	return 0;
}
