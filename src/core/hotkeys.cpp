#include "hotkeys.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMetaObject>
#include <QString>

namespace ovl {

struct Hotkeys::Entry {
	Hotkeys *owner;
	const char *action, *label;
	const char *key; // default binding, e.g. "OBS_KEY_Z"
	bool alt, shift, ctrl;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	bool loaded = false;
};

static const char *SAVE_KEY = "obs_overlay_hotkeys";

static void on_hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed) return;
	auto *e = static_cast<Hotkeys::Entry *>(data); // nested type is private: access granted via friend-less cast below
	QMetaObject::invokeMethod(e->owner, [e] { emit e->owner->triggered(QString::fromLatin1(e->action)); }, Qt::QueuedConnection);
}

Hotkeys::Hotkeys(QObject *parent) : QObject(parent) {}
Hotkeys::~Hotkeys() { unregister_all(); }

void Hotkeys::register_all()
{
	if (!entries_.empty()) return;
	// action id, label shown in OBS, default key (Alt+Z etc. as in the standalone app)
	static const struct { const char *a, *l, *k; bool alt, shift, ctrl; } defs[] = {
		{"toggle_overlay", "OBS Overlay: Show / hide overlay", "OBS_KEY_Z", true, false, false},
		{"record", "OBS Overlay: Start / stop recording", "OBS_KEY_F9", true, false, false},
		{"replay_toggle", "OBS Overlay: Toggle Instant Replay", "OBS_KEY_F10", true, true, false},
		{"replay_save", "OBS Overlay: Save replay", "OBS_KEY_F10", true, false, false},
		{"screenshot", "OBS Overlay: Screenshot (OBS scene)", "OBS_KEY_F1", true, false, false},
		{"mic_mute", "OBS Overlay: Toggle microphone mute", "OBS_KEY_M", true, false, true},
		{"desktop_mute", "OBS Overlay: Toggle desktop audio mute", "OBS_KEY_D", true, false, true},
		{"stats", "OBS Overlay: Show / hide floating widgets", "OBS_KEY_R", true, false, false},
		{"widgets_click", "OBS Overlay: Toggle click-through for all widgets", "OBS_KEY_W", true, true, false},
		{"layout_edit", "OBS Overlay: Edit widget layout", "OBS_KEY_L", true, true, false},
	};
	for (auto &d : defs) {
		auto *e = new Entry{this, d.a, d.l, d.k, d.alt, d.shift, d.ctrl};
		e->id = obs_hotkey_register_frontend(("obs_overlay." + std::string(d.a)).c_str(), d.l, on_hotkey, e);
		entries_.push_back(e);
	}
}

void Hotkeys::unregister_all()
{
	for (auto *e : entries_) {
		if (e->id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(e->id);
		delete e;
	}
	entries_.clear();
}

void Hotkeys::load(void *p)
{
	auto *data = static_cast<obs_data_t *>(p);
	obs_data_t *saved = obs_data_get_obj(data, SAVE_KEY);
	for (auto *e : entries_) {
		obs_data_array_t *arr = saved ? obs_data_get_array(saved, e->action) : nullptr;
		if (!arr) { // no saved binding: install the default only the first time
			if (!e->loaded) {
				arr = obs_data_array_create();
				obs_data_t *b = obs_data_create();
				obs_data_set_string(b, "key", e->key);
				if (e->alt) obs_data_set_bool(b, "alt", true);
				if (e->shift) obs_data_set_bool(b, "shift", true);
				if (e->ctrl) obs_data_set_bool(b, "control", true);
				obs_data_array_push_back(arr, b);
				obs_data_release(b);
			}
		}
		if (arr) {
			obs_hotkey_load(e->id, arr);
			obs_data_array_release(arr);
		}
		e->loaded = true;
	}
	if (saved) obs_data_release(saved);
}

void Hotkeys::save(void *p)
{
	auto *data = static_cast<obs_data_t *>(p);
	obs_data_t *out = obs_data_create();
	for (auto *e : entries_) {
		obs_data_array_t *arr = obs_hotkey_save(e->id);
		obs_data_set_array(out, e->action, arr);
		obs_data_array_release(arr);
	}
	obs_data_set_obj(data, SAVE_KEY, out);
	obs_data_release(out);
}

} // namespace ovl
