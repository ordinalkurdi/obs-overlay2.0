// Widget layout presets, snapping and default arrangement. Pure logic (no Qt, no libobs) so it is unit-tested.
//
// The live layout is the list of widgets in Settings; a preset is a named copy of such a list. Preset metadata
// (id, name, timestamps) is kept apart from the layout it holds, so renaming never touches a layout and overwriting
// never touches the name. A preset is only ever written when the user explicitly saves or overwrites it.
#pragma once
#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace ovl::layouts {

constexpr int MAX_NAME = 40;
constexpr size_t MAX_PRESETS = 50;

// Everything that makes up the look and arrangement of one widget.
struct WidgetCfg {
	std::string id, kind, name;
	int x = 60, y = 60, w = 200, h = 100;
	double opacity = 0.9;
	bool pinned = false, click_through = false, visible = true;
	std::vector<std::string> metrics;
	std::string url;
	double zoom = 1.0;
	bool transparent = true;

	bool operator==(const WidgetCfg &o) const
	{
		return id == o.id && kind == o.kind && name == o.name && x == o.x && y == o.y && w == o.w && h == o.h &&
		       opacity == o.opacity && pinned == o.pinned && click_through == o.click_through && visible == o.visible &&
		       metrics == o.metrics && url == o.url && zoom == o.zoom && transparent == o.transparent;
	}
	bool operator!=(const WidgetCfg &o) const { return !(*this == o); }
};

struct Layout {
	std::vector<WidgetCfg> widgets;
};

struct Preset {
	std::string id, name;
	double created = 0, updated = 0;
	Layout layout;
};

struct PresetState {
	std::vector<Preset> presets;
	std::string active; // id of the preset last loaded / saved ("" = none)
};

// ---------------------------------------------------------------------------------------------------------------
// text helpers
// ---------------------------------------------------------------------------------------------------------------
inline int utf8_len(const std::string &s)
{
	int n = 0;
	for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
	return n;
}

inline std::string utf8_truncate(const std::string &s, int max_chars)
{
	int n = 0;
	for (size_t i = 0; i < s.size(); ++i) {
		if (((unsigned char)s[i] & 0xC0) != 0x80 && ++n > max_chars) return s.substr(0, i);
	}
	return s;
}

inline std::string lower(std::string s)
{
	for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
	return s;
}

// Collapses every run of whitespace to one space and trims the ends.
inline std::string normalize_name(const std::string &name)
{
	std::string out;
	bool pending = false;
	for (unsigned char c : name) {
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
			pending = !out.empty();
		} else {
			if (pending) out += ' ';
			pending = false;
			out += (char)c;
		}
	}
	return out;
}

inline double now_seconds()
{
	using namespace std::chrono;
	return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

inline std::string new_id(size_t n = 10)
{
	static std::mt19937_64 rng{std::random_device{}()};
	static const char *hex = "0123456789abcdef";
	std::string s;
	for (size_t i = 0; i < n; ++i) s += hex[rng() & 15];
	return s;
}

// ---------------------------------------------------------------------------------------------------------------
// layouts
// ---------------------------------------------------------------------------------------------------------------
inline bool is_http_url(const std::string &u)
{
	std::string l = lower(u.substr(0, 8));
	return l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0;
}

// Validates a layout; drops unusable entries (duplicate ids, web docks without a proper address) and clamps values.
inline Layout sanitize_layout(const Layout &raw)
{
	Layout out;
	std::set<std::string> seen;
	for (WidgetCfg w : raw.widgets) {
		if (w.id.empty() || w.kind.empty() || seen.count(w.id)) continue;
		w.w = std::max(40, w.w);
		w.h = std::max(40, w.h);
		w.opacity = std::max(0.2, std::min(1.0, w.opacity));
		if (w.kind == "web" && !is_http_url(w.url)) continue;
		seen.insert(w.id);
		out.widgets.push_back(std::move(w));
	}
	return out;
}

// True when two layouts describe the same arrangement (order of widgets is irrelevant, id decides).
inline bool layouts_equal(const Layout &a, const Layout &b)
{
	if (a.widgets.size() != b.widgets.size()) return false;
	std::map<std::string, const WidgetCfg *> m;
	for (auto &w : a.widgets) m[w.id] = &w;
	if (m.size() != a.widgets.size()) return false;
	for (auto &w : b.widgets) {
		auto it = m.find(w.id);
		if (it == m.end() || *it->second != w) return false;
	}
	return true;
}

using Rect = std::tuple<int, int, int, int>; // x, y, w, h

// Default arrangement: widgets are stacked down the right edge of `area`, starting a new column to the left whenever
// the next one would not fit. Returns one (x, y) per size, in the same order.
inline std::vector<std::pair<int, int>> default_positions(const std::vector<std::pair<int, int>> &sizes, Rect area, int gap = 12)
{
	auto [ax, ay, aw, ah] = area;
	std::vector<std::pair<int, int>> pos;
	int col_x = ax + aw - gap, y = ay + gap, col_w = 0;
	for (auto [w, h] : sizes) {
		if (y > ay + gap && y + h > ay + ah - gap) { // does not fit below: next column to the left
			col_x -= col_w + gap;
			y = ay + gap;
			col_w = 0;
		}
		col_w = std::max(col_w, w);
		pos.emplace_back(std::max(ax, col_x - w), y); // right-aligned inside the column
		y += h + gap;
	}
	return pos;
}

// ---------------------------------------------------------------------------------------------------------------
// snapping (drop indicators while dragging)
// ---------------------------------------------------------------------------------------------------------------
inline int floor_div(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

struct Guide {
	char axis; // 'v' | 'h'
	int pos;
};

struct SnapResult {
	int dx = 0, dy = 0;
	std::vector<Guide> guides;
};

// Snaps a dragged rect to the screen edges / centre and to the edges of other widgets. Each axis snaps to the single
// closest candidate within `threshold`.
inline SnapResult snap_rect(Rect rect, const std::vector<Rect> &others, Rect bounds, int threshold = 10, int gap = 8)
{
	auto [x, y, w, h] = rect;
	auto [bx, by, bw, bh] = bounds;
	std::vector<std::pair<int, int>> xs = {{bx, bx}, {bx + bw - w, bx + bw}, {bx + floor_div(bw - w, 2), bx + floor_div(bw, 2)}};
	std::vector<std::pair<int, int>> ys = {{by, by}, {by + bh - h, by + bh}, {by + floor_div(bh - h, 2), by + floor_div(bh, 2)}};
	for (auto [ox, oy, ow, oh] : others) {
		xs.insert(xs.end(), {{ox, ox}, {ox + ow - w, ox + ow}, {ox + ow + gap, ox + ow}, {ox - w - gap, ox}});
		ys.insert(ys.end(), {{oy, oy}, {oy + oh - h, oy + oh}, {oy + oh + gap, oy + oh}, {oy - h - gap, oy}});
	}
	auto best = [&](const std::vector<std::pair<int, int>> &c, int cur) -> std::optional<std::pair<int, int>> {
		const std::pair<int, int> *b = &c[0];
		for (auto &t : c) if (std::abs(t.first - cur) < std::abs(b->first - cur)) b = &t;
		if (std::abs(b->first - cur) <= threshold) return *b;
		return std::nullopt;
	};
	auto bxm = best(xs, x), bym = best(ys, y);
	SnapResult r;
	r.dx = bxm ? bxm->first - x : 0;
	r.dy = bym ? bym->first - y : 0;
	if (bxm) r.guides.push_back({'v', bxm->second});
	if (bym) r.guides.push_back({'h', bym->second});
	return r;
}

// Keeps at least `keep` px of a widget inside `bounds` so it can never be lost off-screen.
inline std::pair<int, int> clamp_to(Rect rect, Rect bounds, int keep = 60)
{
	auto [x, y, w, h] = rect;
	(void)h;
	auto [bx, by, bw, bh] = bounds;
	return {std::max(bx - w + keep, std::min(x, bx + bw - keep)), std::max(by, std::min(y, by + bh - keep))};
}

// ---------------------------------------------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------------------------------------------
struct PresetError : std::runtime_error { // the message is user-facing (shown inline in the name dialog)
	using std::runtime_error::runtime_error;
};

// CRUD on top of a PresetState. `save` is called after every mutation, so presets survive a restart without any
// extra step.
class PresetStore {
public:
	PresetStore(PresetState &st, std::function<void()> save) : st_(st), save_(std::move(save)) { sanitize(); }

	std::vector<Preset> &presets() { return st_.presets; }
	const std::vector<Preset> &presets() const { return st_.presets; }

	Preset *get(const std::string &id)
	{
		for (auto &p : st_.presets) if (p.id == id) return &p;
		return nullptr;
	}
	Preset *active() { return get(st_.active); }
	const std::string &active_id() const { return st_.active; }

	// Returns the cleaned name or throws PresetError. Names are unique ignoring case and surrounding spaces.
	std::string validate_name(const std::string &name, const std::string &exclude_id = "")
	{
		std::string n = normalize_name(name);
		if (n.empty()) throw PresetError("Enter a name for the preset.");
		if (utf8_len(n) > MAX_NAME) throw PresetError("Use at most " + std::to_string(MAX_NAME) + " characters.");
		for (auto &p : st_.presets)
			if (lower(p.name) == lower(n) && p.id != exclude_id) throw PresetError("A preset with this name already exists.");
		return n;
	}

	// True when the live layout differs from the active preset (so the UI can show "modified").
	bool is_modified(const Layout &current)
	{
		Preset *a = active();
		return a && !layouts_equal(a->layout, current);
	}

	Preset &create(const std::string &name, const Layout &layout)
	{
		std::string n = validate_name(name);
		if (st_.presets.size() >= MAX_PRESETS) throw PresetError("You can keep at most " + std::to_string(MAX_PRESETS) + " presets.");
		double now = now_seconds();
		st_.presets.push_back({new_id(), n, now, now, layout});
		st_.active = st_.presets.back().id;
		save_();
		return st_.presets.back();
	}

	Preset &rename(const std::string &id, const std::string &name)
	{
		Preset *p = get(id);
		if (!p) throw PresetError("This preset no longer exists.");
		p->name = validate_name(name, id); // the layout is not touched
		save_();
		return *p;
	}

	Preset &overwrite(const std::string &id, const Layout &layout)
	{
		Preset *p = get(id);
		if (!p) throw PresetError("This preset no longer exists.");
		p->layout = layout;
		p->updated = now_seconds();
		st_.active = id;
		save_();
		return *p;
	}

	void remove(const std::string &id)
	{
		st_.presets.erase(std::remove_if(st_.presets.begin(), st_.presets.end(), [&](const Preset &p) { return p.id == id; }), st_.presets.end());
		if (st_.active == id) st_.active.clear();
		save_();
	}

	void set_active(const std::string &id)
	{
		if (!get(id)) throw PresetError("This preset no longer exists.");
		st_.active = id;
		save_();
	}

	void clear_active()
	{
		if (!st_.active.empty()) { st_.active.clear(); save_(); }
	}

private:
	// Repairs data from hand-edited or corrupt files: bad layouts, empty / duplicate names and ids.
	void sanitize()
	{
		std::vector<Preset> clean;
		std::set<std::string> ids, names;
		for (auto &p : st_.presets) {
			if (p.id.empty() || ids.count(p.id)) continue;
			std::string name = utf8_truncate(normalize_name(p.name), MAX_NAME);
			if (name.empty()) continue;
			std::string base = name;
			int n = 2;
			while (names.count(lower(name))) {
				std::string suffix = " (" + std::to_string(n++) + ")";
				name = utf8_truncate(base, MAX_NAME - (int)suffix.size()) + suffix;
			}
			ids.insert(p.id);
			names.insert(lower(name));
			clean.push_back({p.id, name, p.created, p.updated, sanitize_layout(p.layout)});
		}
		st_.presets = std::move(clean);
		if (!ids.count(st_.active)) st_.active.clear();
	}

	PresetState &st_;
	std::function<void()> save_;
};

} // namespace ovl::layouts
