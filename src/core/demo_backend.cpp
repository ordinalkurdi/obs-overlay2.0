// DemoBackend: a self-contained simulation so every control can be exercised without OBS and without touching real
// recordings. No audio levels and no video are fabricated; stats it cannot know are left empty ("Unavailable").
#include "backend.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QPainter>
#include <QStandardPaths>
#include <algorithm>
#include <chrono>

namespace ovl {

class DemoBackend final : public Backend {
public:
	explicit DemoBackend(const QString &dir) : dir_(dir)
	{
		scenes_ = {"Gameplay", "Just Chatting", "BRB"};
		scene_ = "Gameplay";
		inputs_ = {{"Desktop Audio", "demo_output", false, 0.0, true, true}, {"Mic/Aux", "demo_input", false, -6.0, true, true}};
		items_["Gameplay"] = {{1, "Alerts", true, false, false}, {2, "Webcam", true, false, false}, {3, "Game Capture", true, true, false}};
		items_["Just Chatting"] = {{1, "Chat", true, false, false}, {2, "Webcam", true, false, false}};
		items_["BRB"] = {{1, "BRB Image", true, false, false}};
	}

	bool is_demo() const override { return true; }

	Snapshot snapshot() override
	{
		Snapshot d;
		d.version = "Demo mode (no OBS)";
		d.scenes = scenes_;
		d.scene = scene_;
		d.items = items_[scene_];
		d.inputs = inputs_;
		d.mic = "Mic/Aux";
		d.desktop = "Desktop Audio";
		d.vcam = vcam_;
		d.base_res = d.out_res = std::make_pair(1920, 1080);
		d.fps = 60.0;
		d.replay_active = replay_;
		d.recording = rec_;
		d.paused = paused_;
		d.streaming = stream_;
		const double now = secs();
		if (rec_) {
			d.rec_dur = (paused_ ? pause_at_ : now) - rec_t0_ - paused_total_;
			d.rec_bytes = (uint64_t)(d.rec_dur * 650000); // nominal 5.2 Mbps, labelled simulated by Demo mode
		}
		if (stream_) {
			d.stream_dur = now - stream_t0_;
			d.bitrate_kbps = 6000.0;
			d.dropped_pct = 0.0;
		}
		return d;
	}

	void start_record() override
	{
		rec_ = true; paused_ = false; paused_total_ = 0; rec_t0_ = secs();
		fire(BackendEvent::RecordStarted);
	}
	void stop_record() override
	{
		rec_ = false; paused_ = false;
		QDir().mkpath(dir_);
		QString p = dir_ + "/" + QDateTime::currentDateTime().toString("'Demo_'yyyy-MM-dd_HH-mm-ss'.txt'");
		write_placeholder(p, "Demo mode placeholder - no video was recorded.\n");
		fire(BackendEvent::RecordStopped, p);
	}
	void toggle_pause() override
	{
		if (!rec_) return;
		if (!paused_) { paused_ = true; pause_at_ = secs(); }
		else { paused_ = false; paused_total_ += secs() - pause_at_; }
		fire(BackendEvent::Changed);
	}
	void start_stream() override { stream_ = true; stream_t0_ = secs(); fire(BackendEvent::StreamStarted); }
	void stop_stream() override { stream_ = false; fire(BackendEvent::StreamStopped); }
	void start_replay() override { replay_ = true; fire(BackendEvent::ReplayState, {}, true); }
	void stop_replay() override { replay_ = false; fire(BackendEvent::ReplayState, {}, false); }
	void save_replay() override
	{
		if (!replay_) return;
		QString folder = dir_ + "/Replays";
		QDir().mkpath(folder);
		QString p = folder + "/" + QDateTime::currentDateTime().toString("'Replay_'yyyy-MM-dd_HH-mm-ss'.txt'");
		write_placeholder(p, "Demo replay placeholder.\n");
		fire(BackendEvent::ReplaySaved, p);
	}
	void start_vcam() override { vcam_ = true; fire(BackendEvent::VcamState, {}, true); }
	void stop_vcam() override { vcam_ = false; fire(BackendEvent::VcamState, {}, false); }
	void set_scene(const std::string &name) override { scene_ = name; fire(BackendEvent::SceneChanged, QString::fromStdString(name)); }

	void set_item_enabled(int64_t id, bool on) override { for (auto &i : items_[scene_]) if (i.id == id) i.enabled = on; fire(BackendEvent::Changed); }
	void set_item_locked(int64_t id, bool on) override { for (auto &i : items_[scene_]) if (i.id == id) i.locked = on; fire(BackendEvent::Changed); }
	void set_mute(const std::string &name, bool muted) override { for (auto &i : inputs_) if (i.name == name) i.muted = muted; fire(BackendEvent::Changed); }
	void set_volume_db(const std::string &name, double db) override { for (auto &i : inputs_) if (i.name == name) i.db = db; }
	void set_replay_seconds(int) override {}

	void request_screenshot() override
	{
		QImage img = frame(640, "DEMO - scene: " + QString::fromStdString(scene_));
		QString p = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/obs-overlay-demo-shot.png";
		img.save(p, "PNG");
		fire(BackendEvent::ScreenshotTaken, p);
	}

	QImage preview(int width) override { return frame(width, "DEMO MODE - NOT LIVE"); }

	std::map<std::string, double> meters() override { return {}; }
	void set_meters_enabled(bool) override {}

private:
	static double secs()
	{
		using namespace std::chrono;
		return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
	}
	void fire(BackendEvent::Type t, const QString &text = {}, bool flag = false)
	{
		if (on_event) on_event({t, text, flag});
	}
	static void write_placeholder(const QString &path, const char *text)
	{
		QFile f(path);
		if (f.open(QIODevice::WriteOnly)) f.write(text);
	}
	static QImage frame(int w, const QString &label)
	{
		QImage img(w, w * 9 / 16, QImage::Format_RGB32);
		img.fill(QColor(30, 30, 30));
		QPainter p(&img);
		p.setPen(QColor(180, 180, 180));
		QFont f = p.font();
		f.setPixelSize(std::max(10, w / 28));
		p.setFont(f);
		p.drawText(img.rect(), Qt::AlignCenter, label);
		return img;
	}

	QString dir_;
	std::vector<std::string> scenes_;
	std::string scene_;
	std::vector<InputInfo> inputs_;
	std::map<std::string, std::vector<ItemInfo>> items_;
	bool rec_ = false, paused_ = false, stream_ = false, replay_ = false, vcam_ = false;
	double rec_t0_ = 0, stream_t0_ = 0, pause_at_ = 0, paused_total_ = 0;
};

std::unique_ptr<Backend> make_demo_backend(const QString &media_dir) { return std::make_unique<DemoBackend>(media_dir); }

} // namespace ovl
