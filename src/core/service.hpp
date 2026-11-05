// Service: single source of truth for the UI. Owns the backend, keeps the latest snapshot, turns
// backend events into notifications and runs the few commands that have rules of their own (replay length,
// screenshots with custom folder / format, microphone / desktop roles, hidden audio sources).
//
// Everything runs on the Qt UI thread = OBS main thread, so there is no worker thread and no locking here.
#pragma once
#include "backend.hpp"
#include "settings.hpp"
#include "types.hpp"
#include <QImage>
#include <QObject>
#include <QTimer>
#include <map>
#include <memory>
#include <vector>

namespace ovl {

class Service : public QObject {
	Q_OBJECT
public:
	enum class Status { Connected, Disconnected };

	struct Note {
		QString time, level, text;
	};

	explicit Service(Settings &cfg, QObject *parent = nullptr);
	~Service() override;

	Settings &cfg;
	Snapshot snap;
	SysMetrics sys;
	std::map<std::string, double> meters;
	std::vector<Note> notifications;

	Status status() const { return status_; }
	bool ok() const { return status_ != Status::Disconnected; }
	QString status_text() const;
	QString diagnostics() const;

	// lifecycle
	void start();
	void shutdown();
	void apply_settings(); // re-apply settings that live inside the backend (audio meters)

	// who needs fresh data (polling only runs while somebody looks)
	void set_overlay_visible(bool on);
	void set_widgets_visible(bool on);

	void say(const QString &level, const QString &text); // level: info | success | warn | error
	void refresh();

	// commands (all safe to call when not connected: the user gets a notice instead)
	void toggle_record();
	void toggle_pause();
	void toggle_stream();
	void toggle_replay();
	void save_replay();
	void toggle_vcam();
	void screenshot();
	void set_scene(const QString &name);
	void set_mute(const QString &name, bool muted);
	void set_volume_db(const QString &name, double db);
	void set_item_enabled(qint64 id, bool on);
	void set_item_locked(qint64 id, bool on);
	// Audio sources the mixer should list: follows the current scene, minus ones the user hid.
	std::vector<InputInfo> mixer_inputs() const;
	void set_preview(bool on, int width); // real-time preview of the program output
	void set_audio_hidden(const QString &name, bool hidden);
	int hidden_audio_count() const;

signals:
	void state_changed();
	void notify(const QString &level, const QString &text);
	void media_created(const QString &path);
	void meters_changed();
	void preview_ready(const QImage &img);

private:
	void on_event(const BackendEvent &e);
	void update_poll();
	void pull_meters();
	void finish_screenshot(const QString &obs_path);
	bool need_backend();

	std::unique_ptr<Backend> backend_;
	Status status_ = Status::Disconnected;
	bool overlay_visible_ = false, widgets_visible_ = false;
	bool pending_shot_ = false;
	QTimer poll_, meter_poll_;
};

} // namespace ovl
