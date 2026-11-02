#include "service.hpp"
#include "audio_math.hpp"
#include "sysmetrics.hpp"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <ctime>

namespace ovl {

Service::Service(Settings &c, QObject *parent) : QObject(parent), cfg(c)
{
	poll_.setTimerType(Qt::CoarseTimer);
	connect(&poll_, &QTimer::timeout, this, &Service::refresh);
	meter_poll_.setInterval(50); // OBS reports levels continuously; the UI reads them at ~20/s and interpolates
	connect(&meter_poll_, &QTimer::timeout, this, &Service::pull_meters);
}

Service::~Service() { shutdown(); }

QString Service::status_text() const
{
	switch (status_) {
	case Status::Connected: return tr("Connected to OBS");
	case Status::Demo: return tr("Demo mode");
	default: return tr("Not running");
	}
}

QString Service::diagnostics() const
{
	return backend_ ? QString::fromStdString(backend_->diagnostics()) : tr("no backend");
}

void Service::say(const QString &level, const QString &text)
{
	notifications.push_back({QDateTime::currentDateTime().toString("HH:mm:ss"), level, text});
	if (notifications.size() > 100) notifications.erase(notifications.begin(), notifications.end() - 100);
	emit notify(level, text);
}

// ---- lifecycle --------------------------------------------------------------------------------------------
void Service::start()
{
	shutdown();
	const bool demo = cfg.s("mode") == "demo";
	backend_ = demo ? make_demo_backend(cfg.s("gallery_root") + "/Demo") : make_live_backend();
	backend_->on_event = [this](const BackendEvent &e) { on_event(e); };
	backend_->set_meters_enabled(cfg.b("audio_detect"));
	status_ = demo ? Status::Demo : Status::Connected;
	say("success", demo ? tr("Demo mode active") : tr("Overlay ready"));
	refresh();
	update_poll();
}

void Service::shutdown()
{
	poll_.stop();
	meter_poll_.stop();
	backend_.reset();
	status_ = Status::Disconnected;
	snap = {};
	meters.clear();
	emit state_changed();
}

void Service::set_overlay_visible(bool on)
{
	overlay_visible_ = on;
	update_poll();
	if (on) refresh();
}

void Service::set_widgets_visible(bool on)
{
	widgets_visible_ = on;
	update_poll();
}

// Poll only while something looks at the data: the open panel, visible widgets, or a running recording / stream
// (whose timers must stay correct). Idle = no timers at all.
void Service::update_poll()
{
	const bool busy = ok() && (overlay_visible_ || widgets_visible_ || snap.recording || snap.streaming);
	if (busy) poll_.start(overlay_visible_ ? 500 : qMax(500, cfg.i("stats_interval_ms")));
	else poll_.stop();
	const bool meters_on = ok() && overlay_visible_ && cfg.b("audio_detect") && !demo();
	if (meters_on && !meter_poll_.isActive()) meter_poll_.start();
	if (!meters_on) { meter_poll_.stop(); if (!meters.empty()) meters.clear(); }
}

void Service::pull_meters()
{
	if (!backend_) return;
	meters = backend_->meters();
	emit meters_changed();
}

// ---- state ------------------------------------------------------------------------------------------------
void Service::refresh()
{
	if (!backend_) return;
	snap = backend_->snapshot();
	audio::apply_roles(snap, cfg.s("mic_source").toStdString(), cfg.s("desktop_source").toStdString());
	if (widgets_visible_) sys = sample_system_metrics();
	update_poll();
	emit state_changed();
}

void Service::on_event(const BackendEvent &e)
{
	auto base = [](const QString &p) { return QFileInfo(p).fileName(); };
	switch (e.type) {
	case BackendEvent::Exit:
		poll_.stop(); meter_poll_.stop();
		return;
	case BackendEvent::RecordStarted: say("info", tr("Recording started")); break;
	case BackendEvent::RecordStopped:
		if (!e.text.isEmpty()) {
			say("success", tr("Recording saved: %1").arg(base(e.text)));
			emit media_created(e.text);
		} else {
			say("info", tr("Recording stopped"));
		}
		break;
	case BackendEvent::ReplaySaved:
		say("success", tr("Replay saved: %1").arg(base(e.text)));
		emit media_created(e.text);
		break;
	case BackendEvent::StreamStarted: say("info", tr("Stream started")); break;
	case BackendEvent::StreamStopped: say("info", tr("Stream stopped")); break;
	case BackendEvent::SceneChanged:
		if (!e.text.isEmpty()) say("info", tr("Scene: %1").arg(e.text));
		break;
	case BackendEvent::ScreenshotTaken: finish_screenshot(e.text); break;
	case BackendEvent::Reload: break; // profile / scene collection changed: just re-read everything
	default: break;
	}
	refresh();
}

// ---- commands ---------------------------------------------------------------------------------------------
bool Service::need_backend()
{
	if (backend_ && ok()) return true;
	say("warn", tr("The overlay is not running"));
	return false;
}

void Service::toggle_record()
{
	if (!need_backend()) return;
	snap.recording ? backend_->stop_record() : backend_->start_record();
	refresh();
}

void Service::toggle_pause()
{
	if (need_backend()) { backend_->toggle_pause(); refresh(); }
}

void Service::toggle_stream()
{
	if (!need_backend()) return;
	snap.streaming ? backend_->stop_stream() : backend_->start_stream(); // OBS reports a missing service itself
	refresh();
}

void Service::toggle_replay()
{
	if (!need_backend()) return;
	if (!snap.replay_available) return say("warn", tr("Replay Buffer is not enabled in OBS (Settings > Output > Replay Buffer)"));
	if (snap.replay_active) {
		backend_->stop_replay();
		say("success", tr("Instant Replay off"));
	} else {
		const int secs = cfg.i("replay_seconds");
		backend_->set_replay_seconds(secs); // not fatal if OBS ignores it: it keeps its own configured length
		backend_->start_replay();
		say("success", tr("Instant Replay on (%1 s)").arg(secs));
	}
	refresh();
}

void Service::save_replay()
{
	if (!need_backend()) return;
	if (!snap.replay_active) return say("warn", tr("Instant Replay is off - turn it on first"));
	backend_->save_replay();
}

void Service::toggle_vcam()
{
	if (!need_backend()) return;
	if (!snap.vcam) return say("warn", tr("Virtual camera is not available in this OBS"));
	*snap.vcam ? backend_->stop_vcam() : backend_->start_vcam();
	say("success", *snap.vcam ? tr("Virtual camera off") : tr("Virtual camera on"));
	refresh();
}

void Service::set_scene(const QString &name)
{
	if (!need_backend() || name.isEmpty() || name.toStdString() == snap.scene) return;
	backend_->set_scene(name.toStdString());
	refresh();
}

void Service::set_mute(const QString &name, bool muted)
{
	if (!need_backend() || name.isEmpty()) return;
	backend_->set_mute(name.toStdString(), muted);
	say("info", tr("%1 %2").arg(name, muted ? tr("muted") : tr("unmuted")));
	refresh();
}

void Service::toggle_mute_role(const QString &role)
{
	const std::string &name = role == "mic" ? snap.mic : snap.desktop;
	if (name.empty()) return say("warn", tr("No %1 audio source found in OBS").arg(role));
	bool cur = false;
	for (auto &i : snap.inputs) if (i.name == name) cur = i.muted; // always toggle from the REAL state
	set_mute(QString::fromStdString(name), !cur);
}

void Service::set_role(const QString &role, const QString &name)
{
	if (role != "mic" && role != "desktop") return;
	if (!name.isEmpty()) {
		bool found = false;
		for (auto &i : snap.inputs) if (i.name == name.toStdString()) found = true;
		if (!found) return say("warn", tr("Audio source '%1' is not available in OBS").arg(name));
	}
	const QString other = role == "mic" ? "desktop_source" : "mic_source";
	if (!name.isEmpty() && cfg.s(other.toUtf8().constData()) == name) cfg.set(other, QString()); // one source cannot be both
	cfg.set(role + "_source", name);
	cfg.save();
	say("info", tr("%1: %2").arg(role == "mic" ? tr("Microphone") : tr("Desktop audio"), name.isEmpty() ? tr("auto-detect") : name));
	refresh();
}

void Service::set_volume_db(const QString &name, double db)
{
	if (backend_ && ok()) backend_->set_volume_db(name.toStdString(), db);
}

void Service::set_item_enabled(qint64 id, bool on)
{
	if (need_backend()) { backend_->set_item_enabled(id, on); refresh(); }
}

void Service::set_item_locked(qint64 id, bool on)
{
	if (need_backend()) { backend_->set_item_locked(id, on); refresh(); }
}

// ---- screenshots --------------------------------------------------------------------------------------------
// OBS writes the picture itself (it is the real program output); we then move / convert it to the folder, format and
// file name pattern chosen in the overlay's settings. Only requests made through the overlay are touched: a screenshot
// taken with OBS's own button or hotkey stays where OBS put it.
void Service::screenshot()
{
	if (!need_backend()) return;
	pending_shot_ = true;
	backend_->request_screenshot();
}

void Service::finish_screenshot(const QString &obs_path)
{
	if (obs_path.isEmpty() || !QFileInfo::exists(obs_path)) {
		if (pending_shot_) say("error", tr("Screenshot failed"));
		pending_shot_ = false;
		return;
	}
	if (!pending_shot_) { emit media_created(obs_path); return; }
	pending_shot_ = false;

	const bool jpg = cfg.s("screenshot_format") == "jpg";
	char buf[256] = {};
	std::time_t t = std::time(nullptr);
	std::strftime(buf, sizeof buf, cfg.s("screenshot_pattern").toLocal8Bit().constData(), std::localtime(&t));
	QString stem = QString::fromLocal8Bit(buf);
	if (stem.isEmpty()) stem = QDateTime::currentDateTime().toString("'Screenshot_'yyyyMMdd_HHmmss");
	for (QChar c : QStringLiteral("\\/:*?\"<>|")) stem.remove(c);
	const QString dir = cfg.s("screenshot_dir");
	QDir().mkpath(dir);
	QString dest = QDir(dir).filePath(stem + (jpg ? ".jpg" : ".png"));
	for (int n = 2; QFileInfo::exists(dest); ++n) dest = QDir(dir).filePath(QString("%1 (%2)%3").arg(stem).arg(n).arg(jpg ? ".jpg" : ".png"));

	QImage img(obs_path);
	bool saved = !img.isNull() && img.save(dest, jpg ? "JPG" : "PNG", jpg ? cfg.i("screenshot_quality") : -1);
	if (saved) {
		if (QFileInfo(obs_path).absoluteFilePath() != QFileInfo(dest).absoluteFilePath()) QFile::remove(obs_path);
		say("success", tr("Screenshot of OBS output saved: %1").arg(QFileInfo(dest).fileName()));
		emit media_created(dest);
	} else {
		say("warn", tr("Screenshot kept where OBS saved it: %1").arg(QFileInfo(obs_path).fileName()));
		emit media_created(obs_path);
	}
}

// ---- preview ------------------------------------------------------------------------------------------------
void Service::request_preview()
{
	if (!backend_ || !ok()) return;
	QElapsedTimer t;
	t.start();
	QImage img = backend_->preview(cfg.i("preview_width"));
	if (img.isNull()) return;
	preview_ms = (double)t.nsecsElapsed() / 1e6;
	emit preview_ready(img);
}

} // namespace ovl
