#include "overlay.hpp"
#include "../core/widget_rows.hpp"
#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QWindow>

namespace ovl::ui {

static QString qs(const std::string &s) { return QString::fromStdString(s); }
static const char *GRAY_SLIDER = "QSlider::sub-page:horizontal{background:#555;} QSlider::handle:horizontal{background:#9a9a9a;}";

// ---- edge trigger -------------------------------------------------------------------------------------------------------
EdgeTrigger::EdgeTrigger() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
}

void EdgeTrigger::configure(QScreen *screen, const QString &side, bool enabled, int y, int h)
{
	const QRect g = screen->availableGeometry();
	enabled_ = enabled;
	const int w = 3;
	setGeometry(side == "right" ? g.right() - w + 1 : g.left(), y, w, h > 0 ? h : g.height());
	refresh(false);
}

void EdgeTrigger::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(0, 0, 0, 3)); // alpha > 0 so Windows delivers hover
}

// ---- overlay --------------------------------------------------------------------------------------------------------------
Overlay::Overlay(Service &svc, MediaStore &store) : QWidget(nullptr), svc_(svc), cfg_(svc.cfg), store_(store)
{
	apply_style(this);
	setWindowTitle("OBS OVERLAY");
	setAttribute(Qt::WA_TranslucentBackground);
	setFocusPolicy(Qt::StrongFocus);
	clock_.start();
	build();
	connect(&svc_, &Service::state_changed, this, &Overlay::sync);
	connect(&svc_, &Service::notify, this, [this](const QString &lv, const QString &t) {
		if (cfg_.b("notifications")) toast_->show_msg(lv, t, cfg_.d("notification_seconds"));
	});
	connect(&svc_, &Service::preview_ready, this, &Overlay::set_preview);
	connect(&svc_, &Service::meters_changed, this, &Overlay::update_meters);

	idle_.setSingleShot(true);
	connect(&idle_, &QTimer::timeout, this, &Overlay::auto_hide);
	connect(&pv_, &QTimer::timeout, &svc_, &Service::request_preview);
	anim_ = new QPropertyAnimation(this, "pos", this);
	connect(anim_, &QPropertyAnimation::valueChanged, this, [this] { clip(); });
	connect(anim_, &QPropertyAnimation::finished, this, [this] { if (open_) clearMask(); else { hide(); clearMask(); } });
	edge_ = new EdgeTrigger;
	connect(edge_, &EdgeTrigger::entered, this, &Overlay::show_panel);
	connect(&meter_timer_, &QTimer::timeout, this, &Overlay::meter_tick);
	leave_timer_.setSingleShot(true);
	leave_timer_.setInterval(450);
	connect(&leave_timer_, &QTimer::timeout, this, &Overlay::maybe_leave);
	persist_.setSingleShot(true);
	persist_.setInterval(400);
	connect(&persist_, &QTimer::timeout, this, &Overlay::persist_geometry);

	using E = Qt::Edge;
	struct G { const char *k; Qt::Edges e; Qt::CursorShape c; };
	const G specs[] = {{"l", E::LeftEdge, Qt::SizeHorCursor}, {"r", E::RightEdge, Qt::SizeHorCursor}, {"t", E::TopEdge, Qt::SizeVerCursor},
			   {"b", E::BottomEdge, Qt::SizeVerCursor}, {"tl", E::LeftEdge | E::TopEdge, Qt::SizeFDiagCursor},
			   {"br", E::RightEdge | E::BottomEdge, Qt::SizeFDiagCursor}, {"tr", E::RightEdge | E::TopEdge, Qt::SizeBDiagCursor},
			   {"bl", E::LeftEdge | E::BottomEdge, Qt::SizeBDiagCursor}};
	for (auto &g : specs) grips_[g.k] = new Grip(this, g.e, g.c, [this] { return !pinned; }, [this] { user_resize = true; });

	pinned = cfg_.b("pinned");
	pin_btn_->setChecked(pinned);
	pin(pinned);
	apply_settings();
	restore_sections();
	sync();
	prev_row_->set_sub(preview_sub());
}

Overlay::~Overlay()
{
	delete edge_;
	delete toast_;
}

void Overlay::build()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	panel_ = new QFrame;
	panel_->setObjectName("panel");
	outer->addWidget(panel_);
	auto *v = new QVBoxLayout(panel_);
	v->setContentsMargins(0, 0, 0, 8);
	v->setSpacing(0);

	header_ = new QFrame;
	header_->setObjectName("header");
	auto *hl = new QHBoxLayout(header_);
	hl->setContentsMargins(16, 10, 10, 10);
	title_ = new QLabel("OBS OVERLAY");
	title_->setObjectName("title");
	dot_ = new QLabel;
	dot_->setFixedSize(10, 10);
	pin_btn_ = tool_button("pin", "Keep open (pin)", true);
	auto *cfgb = tool_button("settings", "Settings");
	auto *mini = tool_button("minimize", "Hide to tray");
	auto *close = tool_button("close", "Hide overlay (return to game)");
	back_ = tool_button("arrow-left", "Back to overlay");
	back_->setVisible(false);
	connect(back_, &QToolButton::clicked, this, [this] { show_page("home"); });
	hl->addWidget(back_); hl->addWidget(title_); hl->addWidget(dot_); hl->addStretch();
	for (auto *b : {pin_btn_, cfgb, mini, close}) hl->addWidget(b);
	v->addWidget(header_);
	connect(pin_btn_, &QToolButton::toggled, this, &Overlay::pin);
	connect(cfgb, &QToolButton::clicked, this, &Overlay::open_settings);
	connect(mini, &QToolButton::clicked, this, &Overlay::hide_panel);
	connect(close, &QToolButton::clicked, this, &Overlay::hide_panel);

	stack = new QStackedWidget;
	v->addWidget(stack, 1);
	auto *home = new QWidget;
	home->setObjectName("plain");
	auto *hv = new QVBoxLayout(home);
	hv->setContentsMargins(0, 0, 0, 0);
	hv->setSpacing(0);
	auto *sc = new QScrollArea;
	sc->setWidgetResizable(true);
	sc->setFrameShape(QFrame::NoFrame);
	sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	sc->setWidget(home);
	stack->addWidget(sc);

	// gallery
	gal_row = new Row("film", "Gallery", "Recent captures from all game folders", true, "Open full gallery");
	hv->addWidget(gal_row);
	strip = new GalleryStrip(store_);
	hv->addWidget(strip);
	hv->addWidget(separator());
	// capture
	rec_ = new Row("record", "Record", "", false);
	rec_->set_right_icon("play");
	rep_ = new Row("replay", "Instant Replay", "", true, "OBS Replay Buffer");
	save_ = new Row("save", "Save replay", "", false);
	shot_ = new Row("camera", "Screenshot", "", false, "Captures the OBS scene output, not the desktop/game directly");
	live_ = new Row("broadcast", "Stream", "", false);
	live_->set_right_icon("play");
	pause_ = new Row("pause-circle", "Pause recording", "", false);
	pause_->setVisible(false);
	vcam_ = new Row("webcam", "Virtual camera", "", false);
	for (Row *r : {rec_, pause_, rep_, save_, shot_, live_, vcam_}) hv->addWidget(r);
	hv->addWidget(separator());
	// production
	scene_row_ = new Row("scene", "Scene", "", false);
	scene_box_ = new QComboBox;
	scene_box_->setMinimumWidth(150);
	scene_box_->setToolTip("Switch OBS scene");
	scene_row_->add_tail(scene_box_);
	hv->addWidget(scene_row_);
	prev_row_ = new Row("monitor", "Preview", "");
	hv->addWidget(prev_row_);
	prev_lab_ = new QLabel;
	prev_lab_->setAlignment(Qt::AlignCenter);
	prev_lab_->setVisible(false);
	hv->addWidget(prev_lab_);
	src_row_ = new Row("layers", "Sources", "Show / hide / lock sources of the current scene");
	hv->addWidget(src_row_);
	srcbox_ = new QWidget;
	sl_ = new QVBoxLayout(srcbox_);
	sl_->setContentsMargins(40, 0, 12, 4);
	sl_->setSpacing(0);
	srcbox_->setVisible(false);
	hv->addWidget(srcbox_);
	hv->addWidget(separator());
	// audio
	mix_row_ = new Row("sliders", "Audio", "Mute, volume and live activity of OBS audio sources");
	mix_row_->set_right_icon("chevron-down", FG_DIM);
	hv->addWidget(mix_row_);
	mixer_box_ = new QWidget;
	ml_ = new QVBoxLayout(mixer_box_);
	ml_->setContentsMargins(46, 0, 16, 4);
	hv->addWidget(mixer_box_);
	hv->addWidget(separator());
	stat_row = new Row("dashboard", "Widgets", "", true, "Add and customize floating widgets (stats, web links)");
	hv->addWidget(stat_row);
	footer_ = new QLabel;
	footer_->setObjectName("dim");
	footer_->setContentsMargins(16, 6, 16, 0);
	footer_->setWordWrap(true);
	hv->addWidget(footer_);
	hv->addStretch(1);
	toast_ = new NotificationWindow;

	connect(rec_, &Row::clicked, this, &Overlay::rec_clicked);
	connect(live_, &Row::clicked, this, &Overlay::live_clicked);
	connect(rep_, &Row::clicked, &svc_, &Service::toggle_replay);
	connect(save_, &Row::clicked, &svc_, &Service::save_replay);
	connect(shot_, &Row::clicked, &svc_, &Service::screenshot);
	connect(mix_row_, &Row::clicked, this, &Overlay::toggle_mixer);
	connect(prev_row_, &Row::clicked, this, &Overlay::toggle_preview);
	connect(pause_, &Row::clicked, &svc_, &Service::toggle_pause);
	connect(vcam_, &Row::clicked, &svc_, &Service::toggle_vcam);
	connect(src_row_, &Row::clicked, this, &Overlay::toggle_sources);
	connect(scene_box_, &QComboBox::activated, this, [this] { svc_.set_scene(scene_box_->currentText()); });
}

// ---- docking / pages -----------------------------------------------------------------------------------------------------------
QScreen *Overlay::screen() const
{
	const auto screens = QGuiApplication::screens();
	const int i = cfg_.i("monitor");
	return (i >= 0 && i < screens.size()) ? screens[i] : QGuiApplication::primaryScreen();
}

QRect Overlay::rect_for(bool shown) const
{
	const QRect g = screen()->availableGeometry();
	const int gap = cfg_.i("dock_gap");
	const int w = qMax(320, qMin(cfg_.i("width"), g.width()));
	int h = cfg_.i("dock_height");
	h = h <= 0 ? g.height() - 2 * gap : h;
	h = qMax(320, qMin(h, g.height()));
	if (docked()) {
		const QString va = cfg_.s("dock_valign");
		int y;
		if (va == "top") y = g.top() + gap;
		else if (va == "bottom") y = g.bottom() - gap - h + 1;
		else if (va == "custom") y = g.top() + cfg_.i("dock_y");
		else y = g.top() + (g.height() - h) / 2;
		y = qMax(g.top(), qMin(y, g.bottom() - h + 1));
		const int x = cfg_.s("dock_side") == "right" ? (shown ? g.right() - w + 1 - gap : g.right() + 1) : (shown ? g.left() + gap : g.left() - w);
		return QRect(x, y, w, h);
	}
	const QJsonArray pos = cfg_["pos"].toArray();
	const int x = pos.size() == 2 ? pos[0].toInt() : g.right() - w - 20, y = pos.size() == 2 ? pos[1].toInt() : g.top() + 60;
	return QRect(x, y, w, qMin(h, g.height() - 40));
}

void Overlay::apply_settings()
{
	Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint;
	const bool key = cfg_.b("always_on_top") || docked();
	if (key) flags |= Qt::WindowStaysOnTopHint;
	const bool was = isVisible();
	bool relaunch = false;
	if (!top_key_known_ || key != top_key_) { // changing window flags recreates the window: only when needed
		top_key_known_ = true; top_key_ = key;
		setWindowFlags(flags);
		relaunch = was;
	}
	setWindowOpacity(cfg_.d("opacity"));
	const bool flush = docked() && cfg_.i("dock_gap") == 0;
	panel_->setStyleSheet(flush ? "#panel{border-radius:0px;}" : "");
	header_->setStyleSheet(flush ? "#header{border-radius:0px;}" : "");
	const QRect g = screen()->availableGeometry(), r = rect_for(true);
	setMinimumSize(320, 320);
	setMaximumSize(g.width(), g.height());
	resize(r.size());
	edge_cfg();
	place_grips();
	anim_->setDuration(cfg_.i("anim_ms"));
	if (relaunch) { setGeometry(r); clearMask(); show(); open_ = true; }
	else if (was && open_) setGeometry(r);
	else if (!was) move(r.topLeft());
	edge_->refresh(open_);
}

void Overlay::show_panel()
{
	if (open_ && isVisible()) { raise(); activateWindow(); return; }
	leave_timer_.stop();
	anim_->stop();
	if (docked()) {
		setGeometry(rect_for(false));
		clip();
		show();
		open_ = true;
		if (cfg_.i("anim_ms") > 0) {
			anim_->setStartValue(pos());
			anim_->setEndValue(rect_for(true).topLeft());
			anim_->setEasingCurve(QEasingCurve::OutCubic);
			anim_->start();
		} else {
			move(rect_for(true).topLeft());
			clearMask();
		}
	} else {
		setGeometry(rect_for(true));
		show();
		open_ = true;
	}
	edge_->refresh(true);
	raise();
	activateWindow();
	bump_idle();
}

void Overlay::hide_panel()
{
	if (!isVisible()) return;
	open_ = false;
	edge_->refresh(false);
	anim_->stop();
	if (docked() && cfg_.i("anim_ms") > 0) {
		anim_->setStartValue(pos());
		anim_->setEndValue(rect_for(false).topLeft());
		anim_->setEasingCurve(QEasingCurve::InCubic);
		anim_->start();
	} else {
		hide();
	}
}

// Mask the part of the sliding panel that lies outside this monitor (no bleeding onto neighbouring screens).
void Overlay::clip()
{
	if (!docked()) return;
	const QRect vis = QRect(pos(), size()).intersected(screen()->geometry());
	setMask(vis.isEmpty() ? QRegion(0, 0, 1, 1) : QRegion(vis.translated(-pos())));
}

void Overlay::edge_cfg()
{
	const QRect r = rect_for(true);
	edge_->configure(screen(), cfg_.s("dock_side"), docked() && cfg_.b("edge_reveal"), r.y(), r.height());
	edge_->refresh(open_);
}

void Overlay::place_grips()
{
	const int w = width(), h = height(), t = 6, c = 14;
	struct Geo { const char *k; int x, y, w, h; };
	const Geo geo[] = {{"l", 0, c, t, h - 2 * c}, {"r", w - t, c, t, h - 2 * c}, {"t", c, 0, w - 2 * c, t}, {"b", c, h - t, w - 2 * c, t},
			   {"tl", 0, 0, c, c}, {"br", w - c, h - c, c, c}, {"tr", w - c, 0, c, c}, {"bl", 0, h - c, c, c}};
	QStringList on;
	if (pinned) on = {};                                       // pinned: no resize handles at all
	else if (docked()) on = cfg_.s("dock_side") == "right" ? QStringList{"l", "t", "b", "tl", "bl"} : QStringList{"r", "t", "b", "tr", "br"}; // none at the screen edge
	else on = {"l", "r", "t", "b", "tl", "br", "tr", "bl"};
	for (auto &g : geo) {
		Grip *gr = grips_[g.k];
		gr->setGeometry(g.x, g.y, qMax(1, g.w), qMax(1, g.h));
		gr->setVisible(on.contains(g.k));
		gr->raise();
	}
}

void Overlay::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	place_grips();
	if (user_resize) persist_.start(); // only user drags are saved, not programmatic resizes
}

void Overlay::persist_geometry()
{
	if (!user_resize || !isVisible()) return;
	user_resize = false;
	const QRect g = screen()->availableGeometry();
	cfg_.set("width", width());
	cfg_.set("dock_height", height());
	if (docked()) { cfg_.set("dock_valign", "custom"); cfg_.set("dock_y", y() - g.top()); }
	else cfg_.set("pos", QJsonArray{x(), y()});
	cfg_.save();
	edge_cfg();
}

void Overlay::set_gallery(QWidget *w) { gallery_ = w; stack->addWidget(w); }
void Overlay::set_widgets_page(QWidget *w) { widgets_ = w; stack->addWidget(w); }
void Overlay::set_settings_page(QWidget *w)
{
	drop_settings(false);
	settings_ = w;
	stack->addWidget(w);
	show_page("settings");
}
void Overlay::drop_settings(bool go_home)
{
	if (settings_) { stack->removeWidget(settings_); settings_->deleteLater(); settings_ = nullptr; }
	if (go_home) show_page("home");
}

void Overlay::show_page(const QString &name)
{
	QString t = "OBS OVERLAY";
	if (name == "gallery" && gallery_) { stack->setCurrentWidget(gallery_); t = "GALLERY"; }
	else if (name == "settings" && settings_) { stack->setCurrentWidget(settings_); t = "SETTINGS"; }
	else if (name == "widgets" && widgets_) { stack->setCurrentWidget(widgets_); t = "WIDGETS"; }
	else stack->setCurrentIndex(0);
	title_->setText(t);
	back_->setVisible(t != "OBS OVERLAY");
}

void Overlay::hideEvent(QHideEvent *e)
{
	meter_timer_.stop(); pv_.stop();
	open_ = false;
	edge_->refresh(false);
	if (!docked()) { cfg_.set("pos", QJsonArray{x(), y()}); cfg_.save(); }
	svc_.set_overlay_visible(false);
	emit visibility_changed(false);
	QWidget::hideEvent(e);
}

void Overlay::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	store_.rescan();
	svc_.set_overlay_visible(true);
	update_meters();
	if (preview_on_) { svc_.request_preview(); pv_.start(cfg_.preview_delay()); }
	emit visibility_changed(true);
}

void Overlay::leaveEvent(QEvent *e)
{
	if (docked() && cfg_.b("dock_autohide") && !pinned && stack->currentIndex() == 0) leave_timer_.start();
	QWidget::leaveEvent(e);
}
void Overlay::enterEvent(QEnterEvent *e) { leave_timer_.stop(); QWidget::enterEvent(e); }

void Overlay::maybe_leave()
{
	if (frameGeometry().contains(QCursor::pos()) || QApplication::activePopupWidget() || QApplication::activeModalWidget()) return;
	if (!pinned && stack->currentIndex() == 0) hide_panel();
}

void Overlay::pin(bool on)
{
	pinned = on;
	dragging_ = false;
	pin_btn_->setIcon(icon("pin", on ? cfg_.s("accent") : FG));
	pin_btn_->setToolTip(on ? "Pinned: stays open, size and position locked (click to unpin)" : "Pin: keep open and lock size/position");
	cfg_.set("pinned", on);
	cfg_.save();
	place_grips();
	bump_idle();
}

void Overlay::bump_idle()
{
	const int s = cfg_.i("auto_hide_seconds");
	if (s > 0 && !pinned) idle_.start(s * 1000);
	else idle_.stop();
}

void Overlay::auto_hide() { if (!pinned && !frameGeometry().contains(QCursor::pos())) hide_panel(); }

void Overlay::mouseMoveEvent(QMouseEvent *e)
{
	if (dragging_) {
		QPoint p = e->globalPosition().toPoint() - drag_;
		if (docked()) { // docked: slide along the edge only
			const QRect g = screen()->availableGeometry();
			p.setX(x());
			p.setY(qMax(g.top(), qMin(p.y(), g.bottom() - height() + 1)));
		}
		move(p);
	}
	bump_idle();
}
void Overlay::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton && e->position().y() < 52 && !pinned) { // pinned = position locked
		dragging_ = true;
		drag_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
	}
}
void Overlay::mouseReleaseEvent(QMouseEvent *)
{
	if (dragging_ && docked() && y() != rect_for(true).y()) {
		cfg_.set("dock_valign", "custom");
		cfg_.set("dock_y", y() - screen()->availableGeometry().top());
		cfg_.save();
		edge_cfg();
	}
	dragging_ = false;
}
void Overlay::keyPressEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Escape) { stack->currentIndex() != 0 ? show_page("home") : hide_panel(); } // Esc: back / give focus to the game
	else QWidget::keyPressEvent(e);
}

// ---- actions --------------------------------------------------------------------------------------------------------------------------
bool Overlay::confirm_if(const char *key, const QString &text) { return !cfg_.b(key) || confirm("Confirm", text); }
void Overlay::rec_clicked()
{
	if (svc_.snap.recording && !confirm_if("confirm_stop_record", "Stop recording?")) return;
	svc_.toggle_record();
}
void Overlay::live_clicked()
{
	if (svc_.snap.streaming && !confirm_if("confirm_stop_stream", "Stop the live stream?")) return;
	svc_.toggle_stream();
}

void Overlay::restore_sections()
{
	preview_on_ = cfg_.b("ui_preview_open");
	prev_lab_->setVisible(preview_on_);
	prev_row_->set_right_icon(preview_on_ ? "chevron-down" : "chevron-right", FG_DIM);
	srcbox_->setVisible(cfg_.b("ui_sources_open"));
	src_row_->set_right_icon(cfg_.b("ui_sources_open") ? "chevron-down" : "chevron-right", FG_DIM);
	mixer_box_->setVisible(cfg_.b("ui_audio_open"));
	mix_row_->set_right_icon(cfg_.b("ui_audio_open") ? "chevron-down" : "chevron-right", FG_DIM);
}

void Overlay::remember(const char *key, bool v)
{
	if (cfg_.b(key) != v) { cfg_.set(key, v); cfg_.save(); }
}

void Overlay::toggle_preview()
{
	preview_on_ = !preview_on_;
	prev_lab_->setVisible(preview_on_);
	remember("ui_preview_open", preview_on_);
	prev_row_->set_right_icon(preview_on_ ? "chevron-down" : "chevron-right", FG_DIM);
	if (preview_on_) { svc_.request_preview(); pv_.start(cfg_.preview_delay()); }
	else pv_.stop();
}

void Overlay::toggle_sources()
{
	const bool on = srcbox_->isHidden(); // isHidden, not isVisible: must also work while the panel is hidden
	srcbox_->setVisible(on);
	remember("ui_sources_open", on);
	src_row_->set_right_icon(on ? "chevron-down" : "chevron-right", FG_DIM);
}

void Overlay::toggle_mixer()
{
	const bool on = mixer_box_->isHidden();
	mixer_box_->setVisible(on);
	remember("ui_audio_open", on);
	mix_row_->set_right_icon(on ? "chevron-down" : "chevron-right", FG_DIM);
}

void Overlay::set_sources(const std::vector<ItemInfo> &items)
{
	QString sig;
	for (auto &i : items) sig += QString("%1|%2|%3|%4;").arg(i.id).arg(qs(i.name)).arg(i.enabled).arg(i.locked);
	if (sig == src_sig_) return;
	src_sig_ = sig;
	while (QLayoutItem *it = sl_->takeAt(0)) { if (QWidget *w = it->widget()) w->deleteLater(); delete it; }
	if (items.empty()) {
		auto *l = new QLabel("No sources in this scene");
		l->setObjectName("dim");
		sl_->addWidget(l);
		return;
	}
	for (auto &it : items) {
		auto *row = new QWidget;
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(4);
		auto *eye = tool_button(it.enabled ? "eye" : "eye-off", it.enabled ? "Hide source" : "Show source");
		auto *lock = tool_button(it.locked ? "lock" : "unlock", it.locked ? "Unlock source" : "Lock source (stops accidental moves in OBS)");
		auto *name = new QLabel(qs(it.name) + (it.group ? "  (group)" : ""));
		name->setToolTip(qs(it.name));
		name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		if (!it.enabled) name->setStyleSheet("color:#6f6f6f;");
		const qint64 id = it.id;
		const bool en = it.enabled, lk = it.locked;
		connect(eye, &QToolButton::clicked, this, [this, id, en] { svc_.set_item_enabled(id, !en); });
		connect(lock, &QToolButton::clicked, this, [this, id, lk] { svc_.set_item_locked(id, !lk); });
		h->addWidget(eye); h->addWidget(name, 1); h->addWidget(lock);
		sl_->addWidget(row);
	}
}

QString Overlay::preview_sub() const
{
	QString s = QString("Snapshot every %1 ms").arg(cfg_.preview_delay());
	if (svc_.preview_ms > 0) s += QString(" - last frame %1 ms").arg(svc_.preview_ms, 0, 'f', 0);
	return s + " (not live video)";
}

void Overlay::apply_preview()
{
	if (preview_on_ && isVisible()) { pv_.start(cfg_.preview_delay()); svc_.request_preview(); } // only the timer changes: nothing restarts
	prev_row_->set_sub(preview_sub());
}

void Overlay::set_preview(const QImage &img)
{
	if (img.isNull()) return;
	prev_lab_->setPixmap(QPixmap::fromImage(img).scaledToWidth(qMax(100, width() - 32), Qt::SmoothTransformation));
	prev_row_->set_sub(preview_sub());
}

QString Overlay::rec_sub() const
{
	auto &s = svc_.snap;
	if (!s.recording) return "Start recording";
	return QString("%1 %2 - %3").arg(s.paused ? "Paused" : "REC", qs(rows::elapsed(s.rec_dur)), qs(rows::fmt_size((double)s.rec_bytes)));
}

QString Overlay::stream_sub() const
{
	auto &s = svc_.snap;
	if (!s.streaming) return "Uses the service configured in OBS (stream key never shown)";
	return QString("LIVE %1 - %2 - dropped %3").arg(qs(rows::elapsed(s.stream_dur)),
		s.bitrate_kbps ? QString("%1 kbps").arg(*s.bitrate_kbps, 0, 'f', 0) : QString("bitrate n/a"),
		s.dropped_pct ? QString("%1%").arg(*s.dropped_pct, 0, 'f', 1) : QString("n/a"));
}

// ---- state sync ---------------------------------------------------------------------------------------------------------------------------
void Overlay::sync()
{
	const Snapshot &s = svc_.snap;
	const bool ok = svc_.ok();
	dot_->setStyleSheet(QString("background:%1; border-radius:5px;").arg(svc_.demo() ? "#4aa3ff" : ok ? "#3ecf8e" : "#e5534b"));
	dot_->setToolTip(svc_.status_text());
	for (Row *r : {rec_, rep_, save_, shot_, live_, scene_row_, mix_row_, prev_row_, src_row_}) r->setEnabled(ok);
	const bool rec = s.recording;
	rec_->set_icon(rec ? "stop-circle" : "record", rec ? "#ff5c5c" : FG);
	rec_->set_right_icon(rec ? "stop" : "play");
	rec_->set_texts(rec ? "Stop recording" : "Record", rec_sub(), true, true);
	rep_->set_sub(s.replay_active ? QString("On - last %1 s").arg(cfg_.i("replay_seconds"))
				      : (s.replay_available ? "Off" : "Unavailable: enable Replay Buffer in OBS settings"));
	rep_->set_icon("replay", s.replay_active ? cfg_.s("accent") : FG);
	rep_->setEnabled(ok && s.replay_available);
	save_->setEnabled(ok && s.replay_active);
	save_->set_sub(s.replay_active ? "Save the last seconds as a clip" : "Turn on Instant Replay first");
	shot_->set_sub("OBS scene output");
	const bool live = s.streaming;
	live_->set_texts(live ? "Stop streaming" : "Start streaming", stream_sub(), true, true);
	live_->set_icon("broadcast", live ? "#ff5c5c" : FG);
	live_->set_right_icon(live ? "stop" : "play");
	{
		QSignalBlocker b(scene_box_);
		scene_box_->clear();
		for (auto &sc : s.scenes) scene_box_->addItem(qs(sc));
		if (!s.scene.empty()) scene_box_->setCurrentText(qs(s.scene));
	}
	scene_row_->set_sub(ok ? "" : "Not running");
	mixer(s.inputs);
	stat_row->set_sub(QString(cfg_.b("stats_visible") ? "On" : "Off") + " - floating stats and web links");
	set_sources(s.items);
	src_row_->set_sub(ok ? QString("%1 in '%2'").arg(s.items.size()).arg(qs(s.scene)) : "Not running");
	src_row_->set_right_icon(srcbox_->isHidden() ? "chevron-right" : "chevron-down", FG_DIM);
	pause_->setVisible(rec);
	pause_->setEnabled(ok);
	pause_->set_texts(s.paused ? "Resume recording" : "Pause recording", "Pauses the OBS recording file", true, true);
	vcam_->set_sub(s.vcam ? (*s.vcam ? "On" : "Off") : "Unavailable in this OBS");
	vcam_->set_icon("webcam", s.vcam && *s.vcam ? cfg_.s("accent") : FG);
	vcam_->setEnabled(ok && s.vcam.has_value());
	footer_->setText((svc_.status_text() + "  |  " + qs(s.version)).trimmed());
}

void Overlay::source_menu(const QString &name, const QPoint &pos)
{
	QMenu m(this);
	apply_style(&m);
	m.addAction("Use as microphone", this, [this, name] { svc_.set_role("mic", name); });
	m.addAction("Use as desktop audio", this, [this, name] { svc_.set_role("desktop", name); });
	m.addAction("Auto-detect microphone / desktop", this, [this] { svc_.set_role("mic", ""); svc_.set_role("desktop", ""); });
	m.exec(pos);
}

// Sources that really carry audio: capture devices always; other source types (browser, media, game...) only once OBS
// reports level meters for them. Before any meter data has arrived they are shown so nothing seems missing.
std::vector<InputInfo> Overlay::shown_inputs(const std::vector<InputInfo> &inputs) const
{
	if (!cfg_.b("audio_detect") || svc_.meters.empty()) return inputs;
	std::vector<InputInfo> out;
	for (auto &i : inputs) if (i.audio_flag || svc_.meters.count(i.name)) out.push_back(i);
	return out;
}

void Overlay::mixer(const std::vector<InputInfo> &all)
{
	const auto inputs = shown_inputs(all);
	QString sig;
	for (auto &i : inputs) sig += qs(i.name) + "\n";
	if (sig != mix_sig_) {
		while (QLayoutItem *it = ml_->takeAt(0)) { if (QWidget *w = it->widget()) w->deleteLater(); delete it; }
		rows_.clear();
		if (inputs.empty()) {
			auto *l = new QLabel(svc_.ok() ? "No audio sources found" : "Not running");
			l->setObjectName("dim");
			ml_->addWidget(l);
		}
		for (auto &i : inputs) {
			const QString n = qs(i.name);
			auto *box = new QWidget;
			auto *bv = new QVBoxLayout(box);
			bv->setContentsMargins(0, 2, 0, 2);
			bv->setSpacing(2);
			auto *head = new QHBoxLayout;
			head->setSpacing(6);
			auto *mute = tool_button("volume", "Mute", false, 16);
			auto *dot = new QLabel;
			dot->setFixedSize(8, 8);
			dot->setStyleSheet("background:#444; border-radius:4px;");
			auto *name = new QLabel(n);
			name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
			name->setToolTip("Right-click to use this source as microphone or desktop audio");
			name->setContextMenuPolicy(Qt::CustomContextMenu);
			connect(name, &QLabel::customContextMenuRequested, this, [this, n, name](const QPoint &p) { source_menu(n, name->mapToGlobal(p)); });
			head->addWidget(mute); head->addWidget(name, 1); head->addWidget(dot);
			auto *bar = new LevelBar;
			auto *sl = new QSlider(Qt::Horizontal);
			sl->setRange(-60, 0);
			sl->setToolTip("Volume (dB)");
			connect(sl, &QSlider::valueChanged, this, [this, n](int val) { svc_.set_volume_db(n, val); });
			connect(mute, &QToolButton::clicked, this, [this, n] {
				bool cur = false; // always toggle from the REAL OBS state, never from a local flag
				for (auto &x : svc_.snap.inputs) if (qs(x.name) == n) cur = x.muted;
				svc_.set_mute(n, !cur);
			});
			bv->addLayout(head); bv->addWidget(bar); bv->addWidget(sl);
			ml_->addWidget(box);
			rows_[n] = MixRow{mute, dot, name, bar, sl, 0, false, -1};
		}
		mix_sig_ = sig;
	}
	const Snapshot &s = svc_.snap;
	for (auto &i : inputs) {
		auto it = rows_.find(qs(i.name));
		if (it == rows_.end()) continue;
		MixRow &r = it->second;
		r.mute->setIcon(icon(i.muted ? "volume-off" : "volume", i.muted ? "#ff7b7b" : FG));
		r.mute->setToolTip(i.muted ? "Unmute" : "Mute");
		QStringList tags;
		if (s.mic == i.name) tags << "mic";
		if (s.desktop == i.name) tags << "desktop";
		r.name->setText(qs(i.name) + (tags.isEmpty() ? "" : "  (" + tags.join(", ") + ")"));
		const bool inactive = !i.active;
		r.name->setStyleSheet(i.muted || inactive ? "color:#6f6f6f;" : "");
		if (r.inactive != (int)inactive) { // gray slider for sources that are not live in the program scene
			r.inactive = (int)inactive;
			r.slider->setStyleSheet(inactive ? GRAY_SLIDER : "");
			r.slider->setToolTip(inactive ? "Volume (dB) - inactive: not live in the current scene" : "Volume (dB)");
		}
		if (!r.slider->isSliderDown()) { QSignalBlocker b(r.slider); r.slider->setValue((int)qBound(-60.0, i.db, 0.0)); }
	}
	update_meters();
}

// New real OBS level data (~20/s): only sets targets; the meter timer animates smoothly towards them.
void Overlay::update_meters()
{
	if (rows_.empty() || !isVisible()) return;
	const bool on = cfg_.b("audio_detect") && !svc_.demo();
	const auto &levels = svc_.meters;
	const double now = clock_.elapsed() / 1000.0;
	std::vector<std::string> known;
	for (auto &kv : levels) known.push_back(kv.first);
	if (!meter_names_known_ || known != meter_names_) { // OBS told us which sources really have audio: refresh the list
		const bool first = !meter_names_known_;
		meter_names_ = known;
		meter_names_known_ = true;
		if (!svc_.snap.inputs.empty() && (!first || !known.empty())) mixer(svc_.snap.inputs);
	}
	for (auto &[n, r] : rows_) {
		r.bar->setVisible(on);
		r.dot->setVisible(on);
		if (!on) continue;
		auto lv = levels.find(n.toStdString());
		const bool have = lv != levels.end();
		r.bar->set_target(have ? audio::level_percent(lv->second) : 0.0);
		const bool active = have && audio::is_active(lv->second);
		if (active) r.active_until = now + 0.35; // short hold: no flicker between peaks
		r.dot->setToolTip(active ? "Audio detected" : have ? "No signal" : "No level data from OBS yet");
	}
	if (on && !meter_timer_.isActive()) { meter_t_ = now; meter_timer_.start(20); } // ~50 fps while something is moving
}

void Overlay::meter_tick()
{
	if (rows_.empty() || !isVisible()) { meter_timer_.stop(); return; }
	const double now = clock_.elapsed() / 1000.0, dt = qMin(0.1, now - meter_t_);
	meter_t_ = now;
	bool moving = false;
	for (auto &kv : rows_) {
		MixRow &r = kv.second;
		if (r.bar->isVisible()) moving |= r.bar->step(dt);
		const bool lit = now < r.active_until;
		if (lit != r.lit) {
			r.lit = lit;
			r.dot->setStyleSheet(QString("background:%1; border-radius:4px;").arg(lit ? "#4cc38a" : "#444"));
		}
		moving |= lit;
	}
	if (!moving) meter_timer_.stop(); // fully at rest: no timer, no CPU
}

} // namespace ovl::ui
