// Qt-free audio helpers: level conversion, activity detection, mic/desktop role resolution and meter ballistics.
#pragma once
#include "types.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace ovl::audio {

constexpr double ACTIVE_DB = -50.0; // a source counts as "active" above this peak level
constexpr double FLOOR_DB = -60.0;

inline double clamp_db(double db) { return std::isnan(db) ? FLOOR_DB : std::max(FLOOR_DB, std::min(0.0, db)); }

// 0..100 bar value for a peak level in dB (libobs volmeters report dB).
inline int level_percent(double db) { return (int)std::lround((clamp_db(db) - FLOOR_DB) / -FLOOR_DB * 100.0); }
inline bool is_active(double db) { return db > ACTIVE_DB; }
inline double mul_to_db(double mul) { return mul <= 0.0 ? FLOOR_DB : std::max(FLOOR_DB, 20.0 * std::log10(mul)); }

// Applies user-chosen microphone / desktop sources on top of the auto-detected ones. An override that no longer
// exists in OBS is ignored (falls back to auto). One source is never both roles.
inline void apply_roles(Snapshot &s, const std::string &mic_override, const std::string &desktop_override)
{
	auto has = [&](const std::string &n) {
		return !n.empty() && std::any_of(s.inputs.begin(), s.inputs.end(), [&](const InputInfo &i) { return i.name == n; });
	};
	bool mic_ok = has(mic_override), desk_ok = has(desktop_override);
	if (mic_ok) s.mic = mic_override;
	if (desk_ok) s.desktop = desktop_override;
	if (!s.mic.empty() && s.mic == s.desktop) {
		if (mic_ok && !desk_ok) s.desktop.clear();
		else s.mic.clear();
	}
}

// Fast attack, smooth release, peak-hold marker that falls slowly. step(dt) returns true while anything still moves.
class Ballistics {
public:
	static constexpr double ATTACK = 40.0, RELEASE = 5.0, HOLD = 0.6, PEAK_FALL = 30.0;
	double value = 0, peak = 0, target = 0;

	bool step(double dt)
	{
		double k = target > value ? ATTACK : RELEASE;
		value += (target - value) * std::min(1.0, std::max(0.0, dt) * k);
		if (target <= 0.0 && value < 0.4) value = 0.0;
		if (value >= peak) { peak = value; hold_ = HOLD; }
		else if (hold_ > 0) hold_ -= dt;
		else peak = std::max(value, peak - dt * PEAK_FALL);
		return value > 0.2 || peak > 0.2 || std::fabs(target - value) > 0.2;
	}

private:
	double hold_ = 0;
};

} // namespace ovl::audio
