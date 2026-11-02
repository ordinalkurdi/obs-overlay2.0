// Plain data shared by the backend, the service and the UI. No Qt, no libobs: this header is unit-testable.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ovl {

struct InputInfo {
	std::string name, kind;
	bool muted = false;
	double db = 0.0;
	bool active = true;      // live in the program scene
	bool audio_flag = true;  // a "real" audio source (capture device), not a source that merely may carry audio
};

struct ItemInfo {
	int64_t id = 0;
	std::string name;
	bool enabled = true, locked = false, group = false;
};

// Everything the UI needs to know about OBS, gathered in one place. Unknown values stay empty ("Unavailable"),
// they are never invented.
struct Snapshot {
	bool recording = false, paused = false;
	double rec_dur = 0.0;
	uint64_t rec_bytes = 0;
	bool streaming = false;
	double stream_dur = 0.0;
	std::optional<double> bitrate_kbps, dropped_pct;
	bool replay_available = true, replay_active = false;
	std::vector<std::string> scenes;
	std::string scene;
	std::vector<ItemInfo> items;
	std::optional<bool> vcam;
	std::vector<InputInfo> inputs;
	std::string mic, desktop;  // empty = none found
	std::optional<double> fps, obs_cpu;
	std::optional<std::pair<int, int>> base_res, out_res;
	std::string version;
};

struct SysMetrics {
	std::optional<double> cpu, mem;
};

} // namespace ovl
