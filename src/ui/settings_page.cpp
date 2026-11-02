#include "settings_page.hpp"
#include "common.hpp"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>

namespace ovl::ui {

SettingsPage::SettingsPage(Service &svc, std::function<void(bool)> on_change, QWidget *parent)
	: QWidget(parent), svc_(svc), cfg_(svc.cfg), on_change_(std::move(on_change))
{
	setObjectName("plain");
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *sc = new QScrollArea;
	sc->setWidgetResizable(true);
	sc->setFrameShape(QFrame::NoFrame);
	auto *host = new QWidget;
	host->setObjectName("plain");
	col_ = new QVBoxLayout(host);
	col_->setContentsMargins(14, 10, 14, 14);
	col_->setSpacing(10);
	sc->setWidget(host);
	outer->addWidget(sc);

	QWidget *c = section("WINDOW");
	choice(c, "dock_mode", "Window style", "Docked slides out from a screen edge; floating is a normal movable window.", {{"Docked to screen edge", "docked"}, {"Floating", "floating"}});
	choice(c, "dock_side", "Dock side", "Which edge the panel slides from.", {{"Right", "right"}, {"Left", "left"}});
	QList<QPair<QString, QString>> mons;
	const auto screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); ++i) mons.push_back({QString("Monitor %1 (%2x%3)").arg(i + 1).arg(screens[i]->geometry().width()).arg(screens[i]->geometry().height()), QString::number(i)});
	choice(c, "monitor", "Monitor", "Which screen the overlay lives on.", mons);
	toggle(c, "edge_reveal", "Open from screen edge", "Touch the screen edge with the mouse to slide the panel out.");
	toggle(c, "dock_autohide", "Hide when the mouse leaves", "Slides away on its own (unless pinned).");
	toggle(c, "always_on_top", "Always on top", "Stay above fullscreen-windowed games.");
	toggle(c, "start_hidden", "Start hidden", "Only the screen edge / hotkey / tray opens it.");
	toggle(c, "show_tray", "Tray icon", "Quick menu in the system tray (OBS keeps its own tray icon).");
	slider(c, "width", 320, 800, "Width", "", " px");
	slider(c, "dock_height", 0, 1600, "Height", "0 = full height of the screen", " px");
	choice(c, "dock_valign", "Vertical position", "Where the docked panel sits on the edge.", {{"Top", "top"}, {"Center", "center"}, {"Bottom", "bottom"}, {"Custom (as dragged)", "custom"}});
	slider(c, "dock_gap", 0, 60, "Gap to screen edge", "", " px");
	slider(c, "anim_ms", 0, 600, "Slide animation", "0 = instant", " ms");
	slider(c, "auto_hide_seconds", 0, 120, "Auto-hide after idle", "0 = never", " s");
	spin(c, "opacity", 0.4, 1.0, 0.02, "Panel opacity", "");

	c = section("APPEARANCE");
	text(c, "accent", "Accent color", "Hex color, e.g. #4cc38a");
	slider(c, "font_size", 10, 20, "Text size", "", " px");
	slider(c, "radius", 0, 24, "Corner radius", "", " px");
	slider(c, "row_pad", 2, 16, "Row spacing", "", " px");

	c = section("CAPTURE");
	text(c, "screenshot_dir", "Screenshot folder", "Screenshots made with the overlay are moved here.", true);
	choice(c, "screenshot_format", "Screenshot format", "", {{"PNG (lossless)", "png"}, {"JPG", "jpg"}});
	slider(c, "screenshot_quality", 40, 100, "JPG quality", "", " %");
	text(c, "screenshot_pattern", "File name pattern", "strftime codes: %Y %m %d %H %M %S");
	slider(c, "replay_seconds", 5, 600, "Instant Replay length", "Applied when the replay buffer starts", " s");
	toggle(c, "confirm_stop_record", "Confirm before stopping a recording", "");
	toggle(c, "confirm_stop_stream", "Confirm before stopping the stream", "");
	toggle(c, "notifications", "Show notifications", "Short messages that never take focus or block clicks.");
	choice(c, "notification_position", "Notification position", "Where on the screen notifications appear.",
	       {{"Center", "center"}, {"Top left", "top-left"}, {"Top center", "top-center"}, {"Top right", "top-right"},
		{"Bottom left", "bottom-left"}, {"Bottom center", "bottom-center"}, {"Bottom right", "bottom-right"}});
	slider(c, "notification_seconds", 1, 10, "Notification time", "", " s");

	c = section("GALLERY");
	text(c, "gallery_root", "Gallery folder", "Sub-folders (per game, replays) are scanned too.", true);
	toggle(c, "hide_filenames", "Hide file names", "Show Video 1 / Image 2 instead (for streaming).");
	slider(c, "thumb_size", 100, 260, "Thumbnail size", "", " px");
	auto *clear = new QPushButton("Clear favorites");
	connect(clear, &QPushButton::clicked, this, [this] {
		if (confirm("Clear history", "Remove all favorites? Your files are not touched.", true)) { cfg_.set_list("favorites", {}); cfg_.save(); changed(); }
	});
	c->layout()->addWidget(clear);

	c = section("AUDIO AND PREVIEW");
	toggle(c, "audio_detect", "Live audio meters", "Real levels from OBS for every audio source.", true);
	toggle(c, "audio_hide_inactive", "Hide inactive audio sources", "The mixer follows the scene: only sources live in the current scene are listed. Use the eye button on a row to hide one by hand.");
	QList<QPair<QString, QString>> srcs = {{"Auto-detect", ""}};
	for (auto &i : svc_.snap.inputs) srcs.push_back({QString::fromStdString(i.name), QString::fromStdString(i.name)});
	choice(c, "mic_source", "Microphone source", "Used by the mic status widget and the mic mute hotkey.", srcs);
	choice(c, "desktop_source", "Desktop audio source", "Used by the desktop status widget and mute hotkey.", srcs);

	c = section("WIDGETS");
	slider(c, "stats_interval_ms", 500, 5000, "Widget refresh", "", " ms");
	spin(c, "stats_warn_dropped_pct", 0.1, 50, 0.5, "Warn at dropped frames", "Percent");
	spin(c, "stats_warn_cpu_pct", 10, 100, 5, "Warn at CPU", "Percent");

	c = section("HOTKEYS");
	auto *hk = new QLabel("Hotkeys are part of OBS, so they work in games and are rebound in <b>OBS Settings &gt; Hotkeys</b>. "
			      "Look for the entries starting with <b>OBS Overlay:</b>. Defaults: Alt+Z overlay, Alt+R widgets, Alt+Shift+W click-through, Alt+Shift+L edit layout. "
			      "Recording, replay buffer, screenshot and mute hotkeys are OBS's own (same settings page).");
	hk->setWordWrap(true);
	hk->setObjectName("dim");
	c->layout()->addWidget(hk);

	c = section("DATA AND DIAGNOSTICS");
	auto *row = new QHBoxLayout;
	auto *exp = new QPushButton("Export settings...");
	auto *imp = new QPushButton("Import settings...");
	auto *rst = new QPushButton("Reset everything");
	rst->setObjectName("danger");
	auto *fld = new QPushButton("Open settings folder");
	for (auto *b : {exp, imp, fld, rst}) row->addWidget(b);
	static_cast<QVBoxLayout *>(c->layout())->addLayout(row);
	connect(exp, &QPushButton::clicked, this, [this] {
		const QString f = QFileDialog::getSaveFileName(this, "Export settings", "obs-overlay-settings.json", "JSON (*.json)");
		if (!f.isEmpty() && !cfg_.export_to(f)) message("Export failed", "The file could not be written.", true);
	});
	connect(imp, &QPushButton::clicked, this, [this] {
		const QString f = QFileDialog::getOpenFileName(this, "Import settings", {}, "JSON (*.json)");
		if (f.isEmpty()) return;
		if (cfg_.import_from(f)) { svc_.say("success", "Settings imported"); changed(true); emit closed(); }
		else message("Import failed", "This is not a valid settings file.", true);
	});
	connect(fld, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(cfg_.path()).absolutePath())); });
	connect(rst, &QPushButton::clicked, this, [this] {
		if (!confirm("Reset everything", "Reset all settings, widgets and layout presets to their defaults?", true)) return;
		cfg_.reset();
		changed(true);
		emit closed();
	});
	auto *diag = new QPlainTextEdit;
	diag->setReadOnly(true);
	diag->setMaximumHeight(130);
	QString d = QString("plugin %1\n%2\n%3\nsettings: %4\nrecent messages:\n").arg(OVL_VERSION, svc_.status_text(), svc_.diagnostics(), cfg_.path());
	for (auto &n : svc_.notifications) d += QString("  %1 [%2] %3\n").arg(n.time, n.level, n.text);
	diag->setPlainText(d);
	c->layout()->addWidget(diag);
	col_->addStretch(1);
}

QWidget *SettingsPage::section(const QString &title)
{
	auto *lab = new QLabel(title);
	lab->setObjectName("sectionTitle");
	col_->addWidget(lab);
	auto *card = new QFrame;
	card->setObjectName("card");
	auto *v = new QVBoxLayout(card);
	v->setContentsMargins(12, 8, 12, 10);
	v->setSpacing(10);
	col_->addWidget(card);
	return card;
}

QWidget *SettingsPage::item(QWidget *card, const QString &title, const QString &desc)
{
	auto *w = new QWidget;
	w->setObjectName("setting");
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(2);
	auto *t = new QLabel(title);
	t->setObjectName("settingTitle");
	v->addWidget(t);
	if (!desc.isEmpty()) { auto *d = new QLabel(desc); d->setObjectName("dim"); d->setWordWrap(true); v->addWidget(d); }
	card->layout()->addWidget(w);
	return w;
}

void SettingsPage::changed(bool restart)
{
	cfg_.save();
	if (on_change_) on_change_(restart);
}

void SettingsPage::toggle(QWidget *card, const char *key, const QString &title, const QString &desc, bool restart)
{
	QWidget *w = item(card, title, desc);
	auto *cb = new QCheckBox("Enabled");
	cb->setChecked(cfg_.b(key));
	connect(cb, &QCheckBox::toggled, this, [this, key, restart](bool on) { cfg_.set(key, on); changed(restart); });
	w->layout()->addWidget(cb);
}

void SettingsPage::slider(QWidget *card, const char *key, int lo, int hi, const QString &title, const QString &desc, const QString &suffix, bool restart)
{
	QWidget *w = item(card, title, desc);
	auto *h = new QHBoxLayout;
	auto *sl = new SeekSlider(Qt::Horizontal);
	sl->setRange(lo, hi);
	sl->setValue(qBound(lo, cfg_.i(key), hi));
	auto *val = new QLabel(QString::number(sl->value()) + suffix);
	val->setObjectName("valueLabel");
	val->setMinimumWidth(64);
	val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	auto *timer = new QTimer(sl); // apply after the drag settles, not on every pixel
	timer->setSingleShot(true);
	timer->setInterval(120);
	connect(timer, &QTimer::timeout, this, [this, restart] { changed(restart); });
	connect(sl, &QSlider::valueChanged, this, [this, key, val, suffix, timer](int v) { cfg_.set(key, v); val->setText(QString::number(v) + suffix); timer->start(); });
	h->addWidget(sl, 1);
	h->addWidget(val);
	static_cast<QVBoxLayout *>(w->layout())->addLayout(h);
}

void SettingsPage::spin(QWidget *card, const char *key, double lo, double hi, double step, const QString &title, const QString &desc)
{
	QWidget *w = item(card, title, desc);
	auto *sp = new QDoubleSpinBox;
	sp->setRange(lo, hi);
	sp->setSingleStep(step);
	sp->setDecimals(2);
	sp->setValue(cfg_.d(key));
	connect(sp, &QDoubleSpinBox::valueChanged, this, [this, key](double v) { cfg_.set(key, v); changed(); });
	w->layout()->addWidget(sp);
}

void SettingsPage::choice(QWidget *card, const char *key, const QString &title, const QString &desc, const QList<QPair<QString, QString>> &items, bool restart)
{
	QWidget *w = item(card, title, desc);
	auto *cb = new QComboBox;
	cb->setFocusPolicy(Qt::StrongFocus);
	const QString cur = cfg_[key].isDouble() ? QString::number(cfg_.i(key)) : cfg_.s(key);
	int sel = 0;
	for (int i = 0; i < items.size(); ++i) { cb->addItem(items[i].first, items[i].second); if (items[i].second == cur) sel = i; }
	cb->setCurrentIndex(sel);
	const bool numeric = cfg_[key].isDouble();
	connect(cb, &QComboBox::activated, this, [this, key, cb, numeric, restart] {
		const QString v = cb->currentData().toString();
		if (numeric) cfg_.set(key, v.toInt()); else cfg_.set(key, v);
		changed(restart);
	});
	w->layout()->addWidget(cb);
}

void SettingsPage::text(QWidget *card, const char *key, const QString &title, const QString &desc, bool folder)
{
	QWidget *w = item(card, title, desc);
	auto *h = new QHBoxLayout;
	auto *le = new QLineEdit(cfg_.s(key));
	connect(le, &QLineEdit::editingFinished, this, [this, key, le] { cfg_.set(key, le->text().trimmed()); changed(); });
	h->addWidget(le, 1);
	if (folder) {
		auto *b = new QPushButton("Browse...");
		connect(b, &QPushButton::clicked, this, [this, key, le] {
			const QString d = QFileDialog::getExistingDirectory(this, "Choose folder", le->text());
			if (!d.isEmpty()) { le->setText(d); cfg_.set(key, d); changed(); }
		});
		h->addWidget(b);
	}
	static_cast<QVBoxLayout *>(w->layout())->addLayout(h);
}

} // namespace ovl::ui
