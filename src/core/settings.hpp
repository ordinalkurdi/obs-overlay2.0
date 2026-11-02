// Settings persistence (one JSON file in OBS's per-plugin config folder). Everything stays local; no passwords or
// stream keys are ever stored (the plugin runs inside OBS and needs no WebSocket connection).
#pragma once
#include "layouts.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <vector>

namespace ovl {

class Settings {
public:
	explicit Settings(const QString &path);

	// ---- typed access to simple keys (unknown keys return neutral values) ----
	QJsonValue operator[](const QString &k) const { return d_.value(k); }
	void set(const QString &k, const QJsonValue &v) { d_[k] = v; }
	int i(const char *k) const { return d_.value(QLatin1String(k)).toInt(); }
	double d(const char *k) const { return d_.value(QLatin1String(k)).toDouble(); }
	bool b(const char *k) const { return d_.value(QLatin1String(k)).toBool(); }
	QString s(const char *k) const { return d_.value(QLatin1String(k)).toString(); }
	QStringList list(const char *k) const;
	void set_list(const char *k, const QStringList &l);
	int preview_delay() const; // validated 100-5000 ms

	// ---- structured data ----
	std::vector<layouts::WidgetCfg> &widgets() { return widgets_; }
	layouts::PresetState &presets() { return presets_; }
	layouts::Layout current_layout() const { return layouts::Layout{widgets_}; }

	// ---- persistence ----
	const QString &path() const { return path_; }
	void load();
	bool save();
	void reset();
	bool export_to(const QString &file) const;
	bool import_from(const QString &file);

	static QJsonObject defaults();

private:
	QJsonObject to_json() const;
	void apply_json(const QJsonObject &raw);

	QString path_;
	QJsonObject d_;
	std::vector<layouts::WidgetCfg> widgets_;
	layouts::PresetState presets_;
};

QJsonObject widget_to_json(const layouts::WidgetCfg &w);
bool widget_from_json(const QJsonObject &o, layouts::WidgetCfg &out);

} // namespace ovl
