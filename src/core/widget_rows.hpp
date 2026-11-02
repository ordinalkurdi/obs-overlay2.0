// Pure logic: turns the OBS state snapshot into display rows for each floating widget kind.
// Unknown values are shown as "Unavailable" - never invented.
#pragma once
#include "layouts.hpp"
#include "types.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ovl::rows {

constexpr const char *WHITE = "#f2f2f2", *RED = "#ff7b7b", *GREEN = "#4cc38a", *DIM = "#9a9a9a";
constexpr const char *U = "Unavailable";

struct Row {
	std::string label, value, color;
};
struct Warn {
	double dropped = 2.0, cpu = 85.0;
};

// (key, label) in the fixed order in which statistics are shown
inline const std::vector<std::pair<std::string, std::string>> &metrics()
{
	static const std::vector<std::pair<std::string, std::string>> m = {
		{"connection", "OBS connection"}, {"recording", "Recording status/time"}, {"streaming", "Streaming status/time"},
		{"bitrate", "Bitrate"},           {"dropped", "Dropped frames"},          {"resolution", "Output resolution"},
		{"fps", "FPS"},                   {"obs_cpu", "OBS CPU"},                 {"cpu", "System CPU"},
		{"memory", "System RAM"},         {"replay", "Replay buffer"}};
	return m;
}

struct Kind {
	const char *key, *title;
	int w, h;
};
inline const std::vector<Kind> &kinds()
{
	static const std::vector<Kind> k = {{"stats", "Statistics", 270, 200}, {"recording", "Recording timer", 210, 84},
					    {"stream", "Stream status", 240, 118}, {"scene", "Current scene", 230, 62},
					    {"audio", "Mic / desktop status", 210, 92}, {"web", "Web link", 380, 150}};
	return k;
}
inline const Kind *kind_of(const std::string &key)
{
	for (auto &k : kinds()) if (key == k.key) return &k;
	return nullptr;
}

inline std::string elapsed(double sec)
{
	long s = (long)std::max(sec, 0.0);
	char buf[32];
	std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
	return buf;
}

inline std::string fmt_size(double n)
{
	static const char *u[] = {"B", "KB", "MB", "GB"};
	char buf[32];
	for (int i = 0; i < 4; ++i) {
		if (n < 1024 || i == 3) {
			std::snprintf(buf, sizeof buf, i == 0 ? "%.0f %s" : "%.1f %s", n, u[i]);
			return buf;
		}
		n /= 1024;
	}
	return "";
}

inline std::string f(const char *fmt, double v)
{
	char buf[48];
	std::snprintf(buf, sizeof buf, fmt, v);
	return buf;
}

inline std::string g(double v) // like Python's {:g}
{
	char buf[48];
	std::snprintf(buf, sizeof buf, "%g", v);
	return buf;
}

// Returns [(label, value, color)]. `status_text` is the connection text; `ok` whether OBS is available.
inline std::vector<Row> build(const std::string &kind, const std::vector<std::string> &selected, const Snapshot &s,
			      const std::string &status_text, bool ok, const Warn &warn, const SysMetrics &sys)
{
	if (kind != "stats" && !ok) return {{"OBS", status_text, RED}};
	if (kind == "recording") {
		if (!s.recording) return {{"Recording", "Idle", DIM}};
		return {{s.paused ? "Paused" : "REC", elapsed(s.rec_dur), s.paused ? WHITE : RED}, {"Size", fmt_size((double)s.rec_bytes), WHITE}};
	}
	if (kind == "stream") {
		if (!s.streaming) return {{"Stream", "Offline", DIM}};
		return {{"LIVE", elapsed(s.stream_dur), GREEN},
			{"Bitrate", s.bitrate_kbps ? f("%.0f kbps", *s.bitrate_kbps) : U, WHITE},
			{"Dropped", s.dropped_pct ? f("%.2f%%", *s.dropped_pct) : U, s.dropped_pct && *s.dropped_pct >= warn.dropped ? RED : WHITE}};
	}
	if (kind == "scene") return {{"Scene", s.scene.empty() ? U : s.scene, WHITE}};
	if (kind == "audio") {
		std::vector<Row> out;
		const std::pair<const std::string *, const char *> roles[2] = {{&s.mic, "Mic"}, {&s.desktop, "Desktop"}};
		for (auto &r : roles) {
			const InputInfo *in = nullptr;
			for (auto &i : s.inputs) if (i.name == *r.first) in = &i;
			out.push_back({r.second, !in ? U : (in->muted ? "MUTED" : "LIVE"), !in ? DIM : (in->muted ? RED : GREEN)});
		}
		return out;
	}
	// statistics (user-selectable metrics, in a fixed order)
	std::vector<Row> out;
	auto on = [&](const char *k) { return std::find(selected.begin(), selected.end(), k) != selected.end(); };
	auto add = [&](const char *k, const char *label, std::string v, const char *c = WHITE) { if (on(k)) out.push_back({label, std::move(v), c}); };
	add("connection", "OBS", status_text, ok ? GREEN : RED);
	add("recording", "Recording", s.recording ? std::string(s.paused ? "Paused " : "REC ") + elapsed(s.rec_dur) : "Idle",
	    s.recording ? (s.paused ? WHITE : RED) : DIM);
	add("streaming", "Streaming", s.streaming ? "LIVE " + elapsed(s.stream_dur) : "Offline", s.streaming ? GREEN : DIM);
	add("bitrate", "Bitrate", s.bitrate_kbps ? f("%.0f kbps", *s.bitrate_kbps) : U, s.bitrate_kbps ? WHITE : DIM);
	add("dropped", "Dropped", s.dropped_pct ? f("%.2f%%", *s.dropped_pct) : U,
	    s.dropped_pct ? (*s.dropped_pct >= warn.dropped ? RED : WHITE) : DIM);
	add("resolution", "Output", s.out_res ? std::to_string(s.out_res->first) + "x" + std::to_string(s.out_res->second) : U, s.out_res ? WHITE : DIM);
	add("fps", "FPS", s.fps && *s.fps > 0 ? g(*s.fps) : U, s.fps && *s.fps > 0 ? WHITE : DIM);
	add("obs_cpu", "OBS CPU", s.obs_cpu ? f("%.0f%%", *s.obs_cpu) : U, s.obs_cpu && *s.obs_cpu >= warn.cpu ? RED : (s.obs_cpu ? WHITE : DIM));
	add("cpu", "System CPU", sys.cpu ? f("%.0f%%", *sys.cpu) : U, sys.cpu && *sys.cpu >= warn.cpu ? RED : WHITE);
	add("memory", "System RAM", sys.mem ? f("%.0f%%", *sys.mem) : U);
	add("replay", "Replay buffer", s.replay_available ? (s.replay_active ? "On" : "Off") : "Not enabled in OBS", s.replay_active ? GREEN : DIM);
	return out;
}

} // namespace ovl::rows
