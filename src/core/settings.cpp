#include "settings.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace ovl {

using layouts::WidgetCfg;

static QJsonArray to_array(const QStringList &l)
{
	QJsonArray a;
	for (auto &s : l) a.append(s);
	return a;
}

QJsonObject Settings::defaults()
{
	QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	QJsonObject o;
	o["opacity"] = 0.96;
	o["width"] = 420;
	o["always_on_top"] = true;
	o["dock_mode"] = "docked"; // "docked" (slides from a screen edge) | "floating"
	o["dock_side"] = "right";
	o["monitor"] = 0;
	o["edge_reveal"] = true;
	o["dock_autohide"] = true;
	o["anim_ms"] = 220;
	o["pinned"] = false; // pinned = stays open, size and position locked
	o["dock_height"] = 780; // px, 0 = full height of the work area
	o["dock_valign"] = "center"; // top | center | bottom | custom
	o["dock_y"] = 0;
	o["dock_gap"] = 12;
	o["row_pad"] = 8;
	o["start_hidden"] = true;
	o["show_tray"] = true;
	o["auto_hide_seconds"] = 0;
	o["pos"] = QJsonArray(); // [x, y] of the floating window, empty = default
	o["accent"] = "#4cc38a";
	o["font_size"] = 13;
	o["radius"] = 10;
	o["gallery_root"] = videos;
	o["screenshot_dir"] = pictures + "/ObsOverlay";
	o["screenshot_format"] = "png";
	o["screenshot_quality"] = 90;
	o["screenshot_pattern"] = "Screenshot_%Y-%m-%d_%H-%M-%S";
	o["replay_seconds"] = 60;
	o["confirm_stop_record"] = false;
	o["confirm_stop_stream"] = true;
	o["notifications"] = true;
	o["notification_position"] = "center"; // top-left | top-center | top-right | center | bottom-left | bottom-center | bottom-right
	o["notification_seconds"] = 3;
	o["hide_filenames"] = false;
	o["favorites"] = QJsonArray();
	o["audio_detect"] = true;
	o["audio_hide_inactive"] = true; // mixer lists only sources live in the current scene
	o["audio_hidden"] = QJsonArray(); // sources the user hid by hand
	o["stats_visible"] = false; // master switch for all floating widgets
	o["ui_preview_open"] = false;
	o["ui_sources_open"] = false;
	o["ui_audio_open"] = true;
	o["active_preset"] = "";
	o["stats_metrics"] = to_array({"recording", "streaming", "bitrate", "dropped", "resolution", "fps", "cpu", "memory", "replay", "connection"});
	o["stats_interval_ms"] = 1000;
	o["stats_warn_dropped_pct"] = 2.0;
	o["stats_warn_cpu_pct"] = 85.0;
	o["thumb_size"] = 150;
	return o;
}

Settings::Settings(const QString &path) : path_(path)
{
	d_ = defaults();
	load();
}

QStringList Settings::list(const char *k) const
{
	QStringList out;
	for (auto v : d_.value(QLatin1String(k)).toArray())
		if (v.isString()) out << v.toString();
	return out;
}

void Settings::set_list(const char *k, const QStringList &l) { d_[QLatin1String(k)] = to_array(l); }

// ---- widgets <-> JSON -------------------------------------------------------------------------------------
QJsonObject widget_to_json(const WidgetCfg &w)
{
	QJsonObject o;
	o["id"] = QString::fromStdString(w.id);
	o["kind"] = QString::fromStdString(w.kind);
	o["name"] = QString::fromStdString(w.name);
	o["x"] = w.x; o["y"] = w.y; o["w"] = w.w; o["h"] = w.h;
	o["opacity"] = w.opacity;
	o["pinned"] = w.pinned; o["click_through"] = w.click_through; o["visible"] = w.visible;
	QJsonArray m;
	for (auto &k : w.metrics) m.append(QString::fromStdString(k));
	o["metrics"] = m;
	o["url"] = QString::fromStdString(w.url);
	o["zoom"] = w.zoom;
	o["transparent"] = w.transparent;
	return o;
}

bool widget_from_json(const QJsonObject &o, WidgetCfg &w)
{
	if (!o.value("id").isString() || !o.value("kind").isString()) return false;
	w.id = o["id"].toString().toStdString();
	w.kind = o["kind"].toString().toStdString();
	w.name = o.value("name").toString().toStdString();
	w.x = o.value("x").toInt(60); w.y = o.value("y").toInt(60);
	w.w = o.value("w").toInt(200); w.h = o.value("h").toInt(100);
	w.opacity = o.value("opacity").toDouble(0.9);
	w.pinned = o.value("pinned").toBool(false);
	w.click_through = o.value("click_through").toBool(false);
	w.visible = o.value("visible").toBool(true);
	w.metrics.clear();
	for (auto v : o.value("metrics").toArray())
		if (v.isString()) w.metrics.push_back(v.toString().toStdString());
	w.url = o.value("url").toString().toStdString();
	w.zoom = o.value("zoom").toDouble(1.0);
	w.transparent = o.value("transparent").toBool(true);
	return true;
}

static bool layout_from_json(const QJsonValue &v, layouts::Layout &out)
{
	if (!v.isObject() || !v.toObject().value("widgets").isArray()) return false;
	out.widgets.clear();
	for (auto wv : v.toObject()["widgets"].toArray()) {
		WidgetCfg w;
		if (wv.isObject() && widget_from_json(wv.toObject(), w)) out.widgets.push_back(w);
	}
	out = layouts::sanitize_layout(out);
	return true;
}

static QJsonObject layout_to_json(const layouts::Layout &l)
{
	QJsonArray a;
	for (auto &w : l.widgets) a.append(widget_to_json(w));
	return QJsonObject{{"widgets", a}};
}

// ---- persistence ---------------------------------------------------------------------------------------------
static bool same_kind(const QJsonValue &a, const QJsonValue &b)
{
	if (a.isDouble() && b.isDouble()) return true;
	return a.type() == b.type();
}

void Settings::apply_json(const QJsonObject &raw)
{
	const QJsonObject def = defaults();
	for (auto it = raw.begin(); it != raw.end(); ++it) {
		const QString k = it.key();
		if (k == "widgets") {
			widgets_.clear();
			for (auto v : it.value().toArray()) {
				WidgetCfg w;
				if (v.isObject() && widget_from_json(v.toObject(), w)) widgets_.push_back(w);
			}
			layouts::Layout l = layouts::sanitize_layout(layouts::Layout{widgets_});
			widgets_ = l.widgets;
		} else if (k == "layout_presets") {
			presets_.presets.clear();
			for (auto v : it.value().toArray()) {
				QJsonObject o = v.toObject();
				layouts::Layout l;
				if (!o.value("id").isString() || !layout_from_json(o.value("layout"), l)) continue; // corrupt entry
				presets_.presets.push_back({o["id"].toString().toStdString(), o.value("name").toString().toStdString(),
							    o.value("created").toDouble(), o.value("updated").toDouble(), l});
			}
		} else if (def.contains(k) && same_kind(def[k], it.value())) {
			d_[k] = it.value();
		}
	}
	presets_.active = d_.value("active_preset").toString().toStdString();
}

void Settings::load()
{
	QFile f(path_);
	if (!f.exists()) { layouts::PresetStore(presets_, [] {}); return; }
	QJsonParseError err;
	QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &err) : QJsonDocument();
	f.close();
	if (!doc.isObject()) { // corrupt: keep a backup, use defaults
		QFile::remove(path_ + ".corrupt");
		QFile::rename(path_, path_ + ".corrupt");
		return;
	}
	apply_json(doc.object());
	layouts::PresetStore tidy(presets_, [] {}); // repairs duplicate names / ids from hand-edited files
	(void)tidy;
}

QJsonObject Settings::to_json() const
{
	QJsonObject o = d_;
	QJsonArray ws;
	for (auto &w : widgets_) ws.append(widget_to_json(w));
	o["widgets"] = ws;
	QJsonArray ps;
	for (auto &p : presets_.presets) {
		ps.append(QJsonObject{{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)},
				      {"created", p.created}, {"updated", p.updated}, {"layout", layout_to_json(p.layout)}});
	}
	o["layout_presets"] = ps;
	o["active_preset"] = QString::fromStdString(presets_.active);
	return o;
}

bool Settings::save()
{
	QDir().mkpath(QFileInfo(path_).absolutePath());
	QSaveFile f(path_); // atomic: a crash never leaves a half-written file
	if (!f.open(QIODevice::WriteOnly)) return false;
	f.write(QJsonDocument(to_json()).toJson(QJsonDocument::Indented));
	return f.commit();
}

void Settings::reset()
{
	d_ = defaults();
	widgets_.clear();
	presets_ = {};
	save();
}

bool Settings::export_to(const QString &file) const
{
	QSaveFile f(file);
	if (!f.open(QIODevice::WriteOnly)) return false;
	f.write(QJsonDocument(to_json()).toJson(QJsonDocument::Indented));
	return f.commit();
}

bool Settings::import_from(const QString &file)
{
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) return false;
	QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) return false;
	apply_json(doc.object());
	layouts::PresetStore tidy(presets_, [] {});
	(void)tidy;
	return save();
}

} // namespace ovl
