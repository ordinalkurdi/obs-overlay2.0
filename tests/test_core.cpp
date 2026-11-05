// Pure-logic unit tests (no Qt, no libobs). Build: see tests/CMakeLists.txt, or
//   g++ -std=c++17 -Isrc/core tests/test_core.cpp -o test_core && ./test_core
#include "audio_math.hpp"
#include "layouts.hpp"
#include "weburl.hpp"
#include "widget_rows.hpp"
#include <cstdio>
#include <cstdlib>

using namespace ovl;
using namespace ovl::layouts;

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { ++g_run; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define THROWS(expr) do { ++g_run; bool t_ = false; try { expr; } catch (const PresetError &) { t_ = true; } \
	if (!t_) { ++g_fail; std::printf("FAIL %s:%d  expected PresetError: %s\n", __FILE__, __LINE__, #expr); } } while (0)

static WidgetCfg widget(const std::string &id, int x = 10, int y = 10)
{
	WidgetCfg w;
	w.id = id; w.kind = "stats"; w.name = "W" + id; w.x = x; w.y = y; w.w = 200; w.h = 100; w.opacity = 0.9; w.metrics = {"fps"};
	return w;
}
static Layout lay(std::vector<WidgetCfg> ws) { return Layout{std::move(ws)}; }

static void test_sanitize()
{
	WidgetCfg web = widget("a"); web.kind = "web";
	WidgetCfg web_ok = widget("b"); web_ok.kind = "web"; web_ok.url = "https://x.test";
	WidgetCfg web_js = widget("c"); web_js.kind = "web"; web_js.url = "javascript:alert(1)";
	Layout out = sanitize_layout(lay({web, web_ok, web_js}));
	CHECK(out.widgets.size() == 1 && out.widgets[0].id == "b");
	WidgetCfg dup = widget("a"), bad; // bad has no id
	out = sanitize_layout(lay({widget("a"), bad, dup}));
	CHECK(out.widgets.size() == 1);
	WidgetCfg c = widget("a"); c.w = 1; c.opacity = 7;
	out = sanitize_layout(lay({c}));
	CHECK(out.widgets[0].w >= 40 && out.widgets[0].opacity == 1.0);
}

static void test_equal()
{
	Layout a = lay({widget("a"), widget("b")}), b = lay({widget("b"), widget("a")});
	CHECK(layouts_equal(a, b));
	b.widgets[0].x = 11;
	CHECK(!layouts_equal(a, b));
}

static void test_presets()
{
	int saves = 0;
	PresetState st;
	PresetStore ps(st, [&] { ++saves; });
	Layout l = lay({widget("a", 1, 2), widget("b", 30, 40)});

	Preset p = ps.create("  My   Layout ", l);
	CHECK(p.name == "My Layout" && st.active == p.id && layouts_equal(p.layout, l) && saves > 0);

	THROWS(ps.create("", l));
	THROWS(ps.create("   ", l));
	ps.create("Alpha", l);
	THROWS(ps.create("alpha ", l));
	THROWS(ps.create(std::string(41, 'x'), l));

	// rename keeps the layout and allows the preset's own name
	std::string id = ps.get(p.id)->id;
	Layout before = ps.get(id)->layout;
	ps.rename(id, "My Layout");
	ps.rename(id, "Renamed");
	CHECK(ps.get(id)->name == "Renamed" && layouts_equal(ps.get(id)->layout, before));
	THROWS(ps.rename(id, "ALPHA"));

	// overwrite only touches the target
	Preset *a = ps.get(id);
	std::string other = ps.presets()[1].id;
	ps.overwrite(a->id, lay({widget("z", 5, 5)}));
	CHECK(ps.get(id)->layout.widgets.size() == 1 && ps.get(id)->layout.widgets[0].id == "z");
	CHECK(ps.get(other)->layout.widgets.size() == 2 && ps.get(id)->name == "Renamed");

	// delete clears the active preset only when it is the active one
	std::string active = st.active; // == id (overwrite activates)
	ps.remove(other);
	CHECK(st.active == active);
	ps.remove(active);
	CHECK(st.active.empty() && ps.presets().empty());

	// modified detection
	Layout cur = lay({widget("a", 1, 2)});
	PresetState s2; PresetStore p2(s2, [] {});
	p2.create("A", cur);
	CHECK(!p2.is_modified(cur));
	Layout moved = cur; moved.widgets[0].x += 5;
	CHECK(p2.is_modified(moved));
	p2.clear_active();
	CHECK(!p2.is_modified(moved));
	THROWS(p2.set_active("nope"));

	// sanitising bad stored data
	PresetState s3;
	Preset good{"1", "Same", 0, 0, cur};
	Preset dupname = good; dupname.id = "2";
	Preset noname = good; noname.id = "3"; noname.name = "";
	Preset dupid = good;
	s3.presets = {good, dupname, noname, dupid};
	s3.active = "ghost";
	PresetStore p3(s3, [] {});
	CHECK(s3.presets.size() == 2 && s3.presets[0].name == "Same" && s3.presets[1].name == "Same (2)" && s3.active.empty());
}

static void test_snap()
{
	Rect screen{0, 0, 1000, 800};
	// near the left edge -> snaps to x = 0
	auto r = snap_rect({6, 100, 200, 100}, {}, screen);
	CHECK(r.dx == -6 && r.dy == 0 && r.guides.size() == 1 && r.guides[0].axis == 'v' && r.guides[0].pos == 0);
	// beside another widget: sits 8 px to its right
	r = snap_rect({310, 300, 200, 100}, {{100, 100, 200, 100}}, screen);
	CHECK(310 + r.dx == 308); // 100 + 200 + gap(8)
	// nothing close: no correction
	r = snap_rect({333, 333, 100, 100}, {}, screen);
	CHECK(r.dx == 0 && r.dy == 0 && r.guides.empty());
	// centre snap
	r = snap_rect({397, 333, 200, 100}, {}, screen);
	CHECK(397 + r.dx == 400);
	// negative floor division (centre of an oversized widget)
	CHECK(floor_div(-5, 2) == -3 && floor_div(5, 2) == 2);
}

static void test_positions()
{
	auto pos = default_positions({{200, 300}, {200, 300}, {200, 300}}, {0, 0, 1000, 800}, 12);
	CHECK(pos.size() == 3);
	CHECK(pos[0].first == 1000 - 12 - 200 && pos[0].second == 12);
	CHECK(pos[1].second == 12 + 300 + 12);
	CHECK(pos[2].first < pos[0].first); // third does not fit below: new column to the left
	auto c = clamp_to({5000, 5000, 200, 100}, {0, 0, 1000, 800});
	CHECK(c.first == 940 && c.second == 740);
	c = clamp_to({-5000, -50, 200, 100}, {0, 0, 1000, 800});
	CHECK(c.first == -140 && c.second == 0);
}

static void test_web()
{
	using namespace ovl::web;
	CHECK(normalize_url("example.com/x") == std::optional<std::string>("https://example.com/x"));
	CHECK(normalize_url("  http://a.b  ") == std::optional<std::string>("http://a.b"));
	CHECK(!normalize_url("javascript:alert(1)"));
	CHECK(!normalize_url("mailto:x@y.z"));
	CHECK(!normalize_url("file:///etc/passwd"));
	CHECK(!normalize_url("ftp://x.y"));
	CHECK(!normalize_url("a b.com"));
	CHECK(!normalize_url(""));
	CHECK(normalize_url("localhost:8080/page") == std::optional<std::string>("https://localhost:8080/page"));
	CHECK(host_of("https://www.Twitch.tv/x") == "twitch.tv" && host_of("junk") == "Web link");
	CHECK(preset_url("Twitch chat", " #SomeChannel ") == std::optional<std::string>("https://www.twitch.tv/popout/somechannel/chat?popout="));
	CHECK(!preset_url("Twitch chat", "a"));
	CHECK(!preset_url("Twitch chat", "bad name!"));
	CHECK(preset_url("Twitch Stream Manager", "") == std::optional<std::string>("https://dashboard.twitch.tv/stream-manager"));
}

static void test_rows_audio()
{
	Snapshot s; SysMetrics sys; rows::Warn w;
	auto r = rows::build("recording", {}, s, "Connected", true, w, sys);
	CHECK(r.size() == 1 && r[0].value == "Idle");
	s.recording = true; s.rec_dur = 3725; s.rec_bytes = 5 * 1024 * 1024;
	r = rows::build("recording", {}, s, "", true, w, sys);
	CHECK(r[0].label == "REC" && r[0].value == "1:02:05" && r[1].value == "5.0 MB");
	r = rows::build("scene", {}, s, "Disconnected", false, w, sys);
	CHECK(r.size() == 1 && r[0].label == "OBS" && r[0].value == "Disconnected");
	s.streaming = true; s.dropped_pct = 3.5;
	r = rows::build("stream", {}, s, "", true, w, sys);
	CHECK(r[1].value == "Unavailable" && r[2].color == rows::RED);
	r = rows::build("stats", {"fps", "cpu", "bitrate"}, s, "Connected", true, w, sys);
	CHECK(r.size() == 3 && r[0].label == "Bitrate" && r[1].label == "FPS" && r[2].label == "System CPU"); // fixed order

	CHECK(audio::level_percent(-60) == 0 && audio::level_percent(0) == 100 && audio::level_percent(-30) == 50);
	CHECK(audio::is_active(-40) && !audio::is_active(-55));
	CHECK(std::fabs(audio::mul_to_db(0.5) + 6.0206) < 1e-3);

	audio::Ballistics b; b.target = 80;
	for (int i = 0; i < 10; ++i) b.step(0.02);
	CHECK(b.value > 60 && b.peak >= b.value - 1e-9);
	b.target = 0;
	for (int i = 0; i < 400; ++i) b.step(0.02);
	CHECK(b.value == 0.0 && b.step(0.02) == false);
}

int main()
{
	test_sanitize(); test_equal(); test_presets(); test_snap(); test_positions(); test_web(); test_rows_audio();
	std::printf("%d checks, %d failed\n", g_run, g_fail);
	return g_fail ? 1 : 0;
}
