// The single interface between the overlay and OBS. LiveBackend talks to the running OBS through libobs and the
// frontend API (no WebSocket needed: the plugin lives inside OBS).
//
// All methods are called on the Qt UI thread (the OBS main thread), which is also where frontend events arrive.
#pragma once
#include "types.hpp"
#include <QImage>
#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace ovl {

struct BackendEvent {
	enum Type {
		Changed,         // something changed, re-read the snapshot
		RecordStarted,
		RecordStopped,   // text = path of the finished recording ("" if unknown)
		ReplaySaved,     // text = path of the saved replay
		ScreenshotTaken, // text = path OBS wrote the screenshot to
		StreamStarted,
		StreamStopped,
		ReplayState,     // flag = active
		VcamState,       // flag = active
		SceneChanged,    // text = scene name
		Exit,            // OBS is closing
		Reload,          // profile / scene collection changed: everything is different now
		PreviewFrame,    // image = a new frame of the program output (real-time preview)
	};
	Type type = Changed;
	QString text;
	bool flag = false;
	QImage image;
};

class Backend {
public:
	virtual ~Backend() = default;
	virtual Snapshot snapshot() = 0;

	virtual void start_record() = 0;
	virtual void stop_record() = 0;
	virtual void toggle_pause() = 0;
	virtual void start_stream() = 0;
	virtual void stop_stream() = 0;
	virtual void start_replay() = 0;
	virtual void stop_replay() = 0;
	virtual void save_replay() = 0;
	virtual void start_vcam() = 0;
	virtual void stop_vcam() = 0;
	virtual void set_scene(const std::string &name) = 0;
	virtual void set_item_enabled(int64_t id, bool on) = 0; // items of the current scene
	virtual void set_item_locked(int64_t id, bool on) = 0;
	virtual void set_mute(const std::string &name, bool muted) = 0;
	virtual void set_volume_db(const std::string &name, double db) = 0;
	virtual void set_replay_seconds(int sec) = 0; // applies when the replay buffer starts
	virtual void request_screenshot() = 0;        // result arrives as BackendEvent::ScreenshotTaken
	// Real-time preview: while on, every rendered frame (up to ~30/s) of the program output arrives as PreviewFrame.
	virtual void set_preview(bool on, int width) = 0;

	// Latest peak level per audio source in dB (real data from OBS volume meters).
	virtual std::map<std::string, double> meters() = 0;
	virtual void set_meters_enabled(bool on) = 0;
	virtual std::string diagnostics() { return {}; }

	// Set by the service. (Not called "emit": that is a Qt keyword macro.)
	std::function<void(const BackendEvent &)> on_event;
};

std::unique_ptr<Backend> make_live_backend();

} // namespace ovl
