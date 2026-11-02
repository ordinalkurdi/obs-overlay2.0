#include "widgets_dock.hpp"
#include "../core/weburl.hpp"
#include "../core/widget_rows.hpp"
#include "common.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <obs-frontend-api.h>

namespace ovl::ui {

using layouts::WidgetCfg;

static QString qs(const std::string &s) { return QString::fromStdString(s); }

void open_obs_browser_docks(const QString &url)
{
	QApplication::clipboard()->setText(url);
	if (auto *mw = static_cast<QMainWindow *>(obs_frontend_get_main_window())) {
		for (QAction *a : mw->findChildren<QAction *>()) {
			QString t = a->text();
			t.remove('&');
			if (t.contains("Custom Browser Docks", Qt::CaseInsensitive)) { a->trigger(); return; }
		}
	}
	message("Browser docks", "The address was copied. In OBS open Docks > Custom Browser Docks and paste it there.");
}

// ---- hover bar ---------------------------------------------------------------------------------------------------
namespace {
class MoveButton : public QToolButton {
public:
	explicit MoveButton(DockWidget *d) : QToolButton(d), dock(d) { setCursor(Qt::SizeAllCursor); }
protected:
	void mousePressEvent(QMouseEvent *e) override { if (e->button() == Qt::LeftButton) dock->drag_start(e->globalPosition().toPoint()); }
	void mouseMoveEvent(QMouseEvent *e) override { if (e->buttons() & Qt::LeftButton) dock->drag_move(e->globalPosition().toPoint()); }
	void mouseReleaseEvent(QMouseEvent *) override { dock->drag_end(); }
private:
	DockWidget *dock;
};
} // namespace

HoverBar::HoverBar(DockWidget *dock) : QFrame(dock)
{
	setObjectName("hoverbar");
	setStyleSheet("#hoverbar{background:rgba(48,48,48,225); border-radius:6px;}");
	auto *lay = new QHBoxLayout(this);
	lay->setContentsMargins(2, 2, 2, 2);
	lay->setSpacing(2);
	opacity = new QSlider(Qt::Horizontal, this);
	opacity->setRange(20, 100);
	opacity->setFixedWidth(64);
	opacity->setValue((int)std::lround(dock->w.opacity * 100));
	opacity->setToolTip("Opacity of this widget");
	opacity->setAccessibleName("Widget opacity");
	connect(opacity, &QSlider::valueChanged, this, [this, dock](int v) { dock->set_opacity(v / 100.0, !opacity->isSliderDown()); });
	connect(opacity, &QSlider::sliderReleased, this, [this, dock] { dock->set_opacity(opacity->value() / 100.0); }); // one save when released
	auto *mv = new MoveButton(dock);
	mv->setIcon(icon("move"));
	mv->setIconSize(QSize(14, 14));
	mv->setToolTip("Drag to move");
	move_btn = mv;
	pin = tool_button("pin", "Pin: lock size and position", true, 14);
	ct = tool_button("pointer-off", "Click-through", true, 14);
	menu_btn = tool_button("dots", "More options", false, 14);
	close_btn = tool_button("close", "Remove this widget", false, 14);
	for (QWidget *b : std::initializer_list<QWidget *>{move_btn, opacity, pin, ct, menu_btn, close_btn}) lay->addWidget(b);
	hide();
}

void HoverBar::show_opacity(double op)
{
	if (opacity->isSliderDown()) return;
	QSignalBlocker b(opacity);
	opacity->setValue((int)std::lround(op * 100));
}

// ---- DockWidget -------------------------------------------------------------------------------------------------------
Qt::WindowFlags DockWidget::flags_for(bool ct)
{
	Qt::WindowFlags f = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus;
	return ct ? (f | Qt::WindowTransparentForInput) : f;
}

DockWidget::DockWidget(Service &svc, const WidgetCfg &cfg) : QWidget(nullptr, flags_for(cfg.click_through)), w(cfg), pinned(cfg.pinned), svc_(svc)
{
	apply_style(this);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setMinimumSize(110, 44);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lab_ = new QLabel;
	lab_->setTextFormat(Qt::RichText);
	lab_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	lay->addWidget(lab_);
	if (w.kind == "web") build_web_card(), lay->addWidget(web_card_);
	setGeometry(w.x, w.y, w.w, w.h);
	QWidget *faded = web_card_ ? web_card_ : static_cast<QWidget *>(lab_);
	fx_ = new QGraphicsOpacityEffect(faded);
	faded->setGraphicsEffect(fx_);
	apply_opacity();

	struct G { const char *k; Qt::Edges e; Qt::CursorShape c; };
	using E = Qt::Edge;
	const G specs[] = {{"l", E::LeftEdge, Qt::SizeHorCursor}, {"r", E::RightEdge, Qt::SizeHorCursor}, {"t", E::TopEdge, Qt::SizeVerCursor},
			   {"b", E::BottomEdge, Qt::SizeVerCursor}, {"tl", E::LeftEdge | E::TopEdge, Qt::SizeFDiagCursor},
			   {"br", E::RightEdge | E::BottomEdge, Qt::SizeFDiagCursor}, {"tr", E::RightEdge | E::TopEdge, Qt::SizeBDiagCursor},
			   {"bl", E::LeftEdge | E::BottomEdge, Qt::SizeBDiagCursor}};
	for (auto &g : specs) grips_[g.k] = new Grip(this, g.e, g.c, [this] { return !pinned && !ct_now(); });

	persist_.setSingleShot(true);
	persist_.setInterval(400);
	connect(&persist_, &QTimer::timeout, this, &DockWidget::save_geometry);
	edit_frame_ = new EditFrame(this, svc.cfg.s("accent"));
	hover_ = new HoverBar(this);
	connect(hover_->pin, &QToolButton::toggled, this, &DockWidget::set_pinned);
	connect(hover_->ct, &QToolButton::toggled, this, [this](bool on) {
		set_click_through(on);
		if (on) svc_.say("info", "Widget is click-through. Turn it off with the click-through hotkey (OBS Settings > Hotkeys), the tray menu or the Widgets page");
	});
	connect(hover_->close_btn, &QToolButton::clicked, this, [this] { emit remove_requested(qs(w.id)); });
	connect(hover_->menu_btn, &QToolButton::clicked, this, [this] { menu(hover_->menu_btn->mapToGlobal(QPoint(0, hover_->menu_btn->height()))); });
	if (w.kind == "web") { hover_->show(); }
	sync_pin_ui();
	place_grips();
}

void DockWidget::build_web_card()
{
	web_card_ = new QWidget;
	auto *v = new QVBoxLayout(web_card_);
	v->setContentsMargins(0, 0, 0, 0);
	web_lab_ = new QLabel;
	web_lab_->setWordWrap(true);
	web_lab_->setTextFormat(Qt::RichText);
	web_lab_->setStyleSheet("background:#181818; border-radius:10px; padding:12px 14px; color:#f2f2f2;");
	web_lab_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	auto *row = new QHBoxLayout;
	auto *open = new QPushButton("Open in browser");
	auto *copy = new QPushButton("Copy URL");
	auto *dock = new QPushButton("Add as OBS dock");
	dock->setObjectName("primary");
	dock->setToolTip("Opens OBS's Custom Browser Docks dialog (the address is copied: just paste it)");
	connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(qs(w.url))); });
	connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(qs(w.url)); svc_.say("info", "Address copied"); });
	connect(dock, &QPushButton::clicked, this, [this] { open_obs_browser_docks(qs(w.url)); });
	row->addWidget(open); row->addWidget(copy); row->addWidget(dock);
	v->addWidget(web_lab_, 1);
	v->addLayout(row);
	lab_->hide();
	setMinimumSize(300, 130);
}

void DockWidget::render()
{
	if (w.kind == "web") {
		const QString nm = w.name.empty() ? "Web link" : qs(w.name);
		web_lab_->setText(QString("<b>%1</b><br><span style='color:#9a9a9a'>%2</span>").arg(nm.toHtmlEscaped(), qs(w.url).toHtmlEscaped()));
		return;
	}
	rows::Warn warn{svc_.cfg.d("stats_warn_dropped_pct"), svc_.cfg.d("stats_warn_cpu_pct")};
	auto rs = rows::build(w.kind, w.metrics, svc_.snap, svc_.status_text().toStdString(), svc_.ok(), warn, svc_.sys);
	QString body;
	for (auto &r : rs)
		body += QString("<tr><td style=\"color:#9a9a9a\">%1&nbsp;&nbsp;</td><td style=\"color:%2\">%3</td></tr>")
				.arg(qs(r.label).toHtmlEscaped(), qs(r.color), qs(r.value).toHtmlEscaped());
	if (body.isEmpty()) body = "<tr><td style=\"color:#9a9a9a\">No metrics selected</td></tr>";
	lab_->setText("<table>" + body + "</table>");
	const int fs = qBound(9, width() / 17, 48), pad = qMax(6, fs / 2); // text scales with the widget width
	const QString st = QString("background:#181818; border-radius:%1px; padding:%1px %2px; font-size:%3px; color:#f2f2f2;").arg(pad).arg(pad + 4).arg(fs);
	if (st != style_) { style_ = st; lab_->setStyleSheet(st); }
}

void DockWidget::place_grips()
{
	const int W = width(), H = height(), t = 6, c = 12;
	auto set = [&](const char *k, int x, int y, int w_, int h_) {
		QWidget *g = grips_[k];
		g->setGeometry(x, y, qMax(1, w_), qMax(1, h_));
		g->setVisible(!pinned && !ct_now());
		g->raise();
	};
	set("l", 0, c, t, H - 2 * c); set("r", W - t, c, t, H - 2 * c); set("t", c, 0, W - 2 * c, t); set("b", c, H - t, W - 2 * c, t);
	set("tl", 0, 0, c, c); set("br", W - c, H - c, c, c); set("tr", W - c, 0, c, c); set("bl", 0, H - c, c, c);
	if (edit_frame_) { edit_frame_->setGeometry(rect()); edit_frame_->raise(); }
	if (hover_) { hover_->adjustSize(); hover_->move(width() - hover_->width() - 4, 4); hover_->raise(); }
}

void DockWidget::resizeEvent(QResizeEvent *e) { QWidget::resizeEvent(e); place_grips(); render(); persist_.start(); emit geometry_changed(); }
void DockWidget::moveEvent(QMoveEvent *e) { QWidget::moveEvent(e); persist_.start(); emit geometry_changed(); }

void DockWidget::save_geometry()
{
	const QRect g = geometry();
	w.x = g.x(); w.y = g.y(); w.w = g.width(); w.h = g.height();
	emit needs_save();
}

void DockWidget::sync_pin_ui()
{
	if (!hover_) return;
	auto setb = [](QToolButton *b, bool on, const QString &ico, const QString &tip) {
		QSignalBlocker sb(b);
		b->setChecked(on);
		b->setIcon(icon(ico, on ? "#4cc38a" : FG));
		b->setToolTip(tip);
	};
	setb(hover_->pin, pinned, "pin", pinned ? "Pinned - click to unlock" : "Pin: lock size and position");
	setb(hover_->ct, w.click_through, "pointer-off", w.click_through ? "Click-through is ON" : "Click-through: mouse passes to what is behind");
}

void DockWidget::enterEvent(QEnterEvent *e)
{
	if (hover_ && w.kind != "web" && !ct_now()) { hover_->show(); hover_->raise(); }
	QWidget::enterEvent(e);
}
void DockWidget::leaveEvent(QEvent *e)
{
	if (hover_ && w.kind != "web") hover_->hide();
	QWidget::leaveEvent(e);
}
void DockWidget::mousePressEvent(QMouseEvent *e) { if (e->button() == Qt::LeftButton) drag_start(e->globalPosition().toPoint()); }
void DockWidget::mouseMoveEvent(QMouseEvent *e) { if (e->buttons() & Qt::LeftButton) drag_move(e->globalPosition().toPoint()); }
void DockWidget::mouseReleaseEvent(QMouseEvent *) { drag_end(); }
void DockWidget::contextMenuEvent(QContextMenuEvent *e) { menu(e->globalPos()); }

// ---- dragging with snap guides -------------------------------------------------------------------------------------------
void DockWidget::drag_start(const QPoint &gp)
{
	if (pinned) return; // locked while pinned
	if (anim_) anim_->stop();
	dragging_ = true; drag_origin_ = gp; drag_pos_ = pos();
}
void DockWidget::drag_move(const QPoint &gp)
{
	if (!dragging_) return;
	QPoint p = drag_pos_ + (gp - drag_origin_);
	move(mgr ? mgr->snap_move(this, QRect(p, size())) : p);
}
void DockWidget::drag_end()
{
	if (!dragging_) return;
	dragging_ = false;
	if (mgr) mgr->end_drag(this);
}

// ---- state changes ---------------------------------------------------------------------------------------------------------------
void DockWidget::set_name(const QString &name)
{
	const QString n = name.trimmed();
	w.name = n.isEmpty() ? std::string(rows::kind_of(w.kind)->title) : n.toStdString();
	render();
	emit needs_save();
}

void DockWidget::set_pinned(bool on) { w.pinned = pinned = on; sync_pin_ui(); place_grips(); emit flags_changed(); emit needs_save(); }

void DockWidget::refresh_flags()
{
	const bool vis = isVisible();
	const QRect geo = geometry();
	setWindowFlags(flags_for(ct_now()));
	setGeometry(geo);
	apply_opacity();
	place_grips(); sync_pin_ui();
	if (w.kind == "web") hover_->show();
	else if (ct_now()) hover_->hide();
	if (vis) show();
}

void DockWidget::set_click_through(bool on) { w.click_through = on; refresh_flags(); emit flags_changed(); emit needs_save(); }

void DockWidget::set_edit_mode(bool on)
{
	if (on == edit_) return;
	edit_ = on;
	if (w.click_through) refresh_flags(); // click-through is suspended while editing, so every widget can be grabbed
	edit_frame_->accent = QColor(svc_.cfg.s("accent"));
	edit_frame_->setVisible(on);
	if (ct_now()) hover_->hide();
	place_grips();
}

void DockWidget::apply_config(const WidgetCfg &n, bool animate)
{
	const bool ct_before = w.click_through;
	const std::string id = w.id, kind = w.kind;
	w = n; w.id = id; w.kind = kind;
	pinned = w.pinned;
	if (w.click_through != ct_before) refresh_flags();
	apply_opacity(); sync_pin_ui(); place_grips(); render();
	const QRect target(w.x, w.y, w.w, w.h);
	if (anim_) anim_->stop();
	if (animate && isVisible() && geometry() != target) {
		anim_ = new QPropertyAnimation(this, "geometry", this);
		anim_->setDuration(220);
		anim_->setEasingCurve(QEasingCurve::OutCubic);
		anim_->setStartValue(geometry());
		anim_->setEndValue(target);
		anim_->start(QAbstractAnimation::DeleteWhenStopped);
	} else {
		setGeometry(target);
	}
}

void DockWidget::apply_opacity()
{
	if (hover_) hover_->show_opacity(w.opacity);
	setWindowOpacity(1.0);
	fx_->setOpacity(w.opacity); // fades the content only, never the buttons on top of it
}

void DockWidget::set_opacity(double v, bool commit)
{
	w.opacity = std::round(v * 100.0) / 100.0;
	apply_opacity();
	if (commit) emit needs_save();
}

void DockWidget::set_visible_flag(bool on)
{
	w.visible = on;
	setVisible(on && svc_.cfg.b("stats_visible"));
	emit flags_changed();
	emit needs_save();
}

void DockWidget::toggle_metric(const std::string &key, bool on)
{
	std::vector<std::string> out;
	for (auto &[k, label] : rows::metrics()) {
		const bool has = std::find(w.metrics.begin(), w.metrics.end(), k) != w.metrics.end();
		if ((has && k != key) || (k == key && on)) out.push_back(k);
	}
	w.metrics = out;
	render();
	emit needs_save();
}

void DockWidget::set_url(const QString &url) { w.url = url.toStdString(); render(); emit needs_save(); }

void DockWidget::center_on_screen()
{
	const QRect g = QGuiApplication::primaryScreen()->availableGeometry();
	move(g.center() - QPoint(width() / 2, height() / 2));
}

void DockWidget::menu(const QPoint &pos)
{
	QMenu m(this);
	apply_style(&m);
	QAction *a = m.addAction("Pinned (lock size and position)");
	a->setCheckable(true); a->setChecked(pinned);
	connect(a, &QAction::toggled, this, &DockWidget::set_pinned);
	a = m.addAction("Click-through (mouse passes to the game)");
	a->setCheckable(true); a->setChecked(w.click_through);
	connect(a, &QAction::toggled, this, &DockWidget::set_click_through);
	QMenu *om = m.addMenu("Opacity");
	for (int pct : {100, 85, 70, 55, 40, 25}) {
		QAction *act = om->addAction(QString("%1%").arg(pct));
		act->setCheckable(true); act->setChecked(std::abs(w.opacity * 100 - pct) < 8);
		connect(act, &QAction::triggered, this, [this, pct] { set_opacity(pct / 100.0); });
	}
	if (w.kind == "web") {
		m.addAction("Open in your browser", this, [this] { QDesktopServices::openUrl(QUrl(qs(w.url))); });
		m.addAction("Add as OBS browser dock", this, [this] { open_obs_browser_docks(qs(w.url)); });
	}
	if (w.kind == "stats") {
		QMenu *mm = m.addMenu("Metrics");
		for (auto &[k, label] : rows::metrics()) {
			QAction *act = mm->addAction(qs(label));
			act->setCheckable(true);
			act->setChecked(std::find(w.metrics.begin(), w.metrics.end(), k) != w.metrics.end());
			const std::string key = k;
			connect(act, &QAction::toggled, this, [this, key](bool on) { toggle_metric(key, on); });
		}
	}
	m.addSeparator();
	m.addAction("Center on screen", this, &DockWidget::center_on_screen);
	m.addAction("Remove widget", this, [this] { emit remove_requested(qs(w.id)); });
	m.exec(pos);
}

// ---- WidgetManager -----------------------------------------------------------------------------------------------------------------------
WidgetManager::WidgetManager(Service &svc, QObject *parent) : QObject(parent), svc_(svc), cfg_(svc.cfg)
{
	dirty_.setSingleShot(true);
	dirty_.setInterval(150);
	connect(&dirty_, &QTimer::timeout, this, &WidgetManager::layout_changed);
	connect(&timer_, &QTimer::timeout, this, &WidgetManager::refresh);
	for (WidgetCfg wc : cfg_.widgets()) {
		if (!rows::kind_of(wc.kind)) continue;
		keep_on_screen(wc);
		make(wc);
	}
}

WidgetManager::~WidgetManager()
{
	for (auto *d : docks) { d->hide(); delete d; }
	delete guides_;
}

void WidgetManager::keep_on_screen(WidgetCfg &wc)
{
	const QRect r(wc.x, wc.y, wc.w, wc.h);
	for (QScreen *s : QGuiApplication::screens()) if (s->geometry().intersects(r)) return;
	const QRect g = QGuiApplication::primaryScreen()->availableGeometry(); // was off-screen
	wc.x = g.left() + 40; wc.y = g.top() + 40;
}

DockWidget *WidgetManager::make(const WidgetCfg &wc)
{
	auto *d = new DockWidget(svc_, wc);
	d->mgr = this;
	connect(d, &DockWidget::needs_save, this, [this] { commit(); dirty_.start(); });
	connect(d, &DockWidget::flags_changed, this, &WidgetManager::changed);
	connect(d, &DockWidget::remove_requested, this, &WidgetManager::remove);
	d->set_edit_mode(edit_mode);
	docks.push_back(d);
	return d;
}

DockWidget *WidgetManager::find(const QString &id) const
{
	for (auto *d : docks) if (d->w.id == id.toStdString()) return d;
	return nullptr;
}

void WidgetManager::commit()
{
	cfg_.widgets().clear();
	for (auto *d : docks) cfg_.widgets().push_back(d->w);
	cfg_.save();
}

layouts::Layout WidgetManager::current_layout() const
{
	layouts::Layout l;
	for (auto *d : docks) l.widgets.push_back(d->w);
	return l;
}

// Makes the live widgets match a saved layout without a restart: widgets in both are updated in place (and glide to
// their position), missing ones are created, ones not in the layout are removed.
void WidgetManager::apply_layout(const layouts::Layout &layout, bool animate)
{
	layouts::Layout lay = layouts::sanitize_layout(layout);
	std::vector<WidgetCfg> entries;
	for (auto &w : lay.widgets) if (rows::kind_of(w.kind)) entries.push_back(w);
	for (auto it = docks.begin(); it != docks.end();) {
		auto e = std::find_if(entries.begin(), entries.end(), [&](const WidgetCfg &w) { return w.id == (*it)->w.id && w.kind == (*it)->w.kind; });
		if (e == entries.end()) { (*it)->close(); (*it)->deleteLater(); it = docks.erase(it); }
		else ++it;
	}
	std::vector<DockWidget *> ordered;
	for (WidgetCfg nw : entries) {
		keep_on_screen(nw);
		DockWidget *d = find(QString::fromStdString(nw.id));
		if (d) { d->apply_config(nw, animate); docks.erase(std::find(docks.begin(), docks.end(), d)); }
		else { d = make(nw); docks.pop_back(); }
		ordered.push_back(d);
	}
	docks = ordered;
	cfg_.set("stats_visible", true); // the user asked to see this layout
	commit();
	apply(false);
	svc_.refresh();
	emit changed();
	dirty_.start();
}

void WidgetManager::reset_default_layout()
{
	const QRect g = target_screen()->availableGeometry();
	const int top = g.y() + (edit_mode ? 64 : 0); // leave room for the toolbar
	layouts::Layout lay = current_layout();
	std::vector<std::pair<int, int>> sizes;
	for (auto &w : lay.widgets) sizes.push_back({w.w, w.h});
	auto pos = layouts::default_positions(sizes, {g.x(), top, g.width(), g.height() - (top - g.y())});
	for (size_t i = 0; i < lay.widgets.size(); ++i) { lay.widgets[i].x = pos[i].first; lay.widgets[i].y = pos[i].second; lay.widgets[i].visible = true; }
	apply_layout(lay);
	svc_.say("info", "Widgets arranged in the default layout");
}

void WidgetManager::hide_all() { for (auto *d : docks) d->set_visible_flag(false); }
void WidgetManager::show_all() { for (auto *d : docks) d->set_visible_flag(true); }
bool WidgetManager::all_hidden() const
{
	return !docks.empty() && std::none_of(docks.begin(), docks.end(), [](DockWidget *d) { return d->w.visible; });
}

void WidgetManager::set_edit_mode(bool on)
{
	if (on == edit_mode) return;
	edit_mode = on;
	if (on && !cfg_.b("stats_visible")) { cfg_.set("stats_visible", true); cfg_.save(); } // widgets must be visible to edit
	for (auto *d : docks) d->set_edit_mode(on);
	apply();
	if (!on) { if (guides_) guides_->hide(); cfg_.save(); }
	emit edit_mode_changed(on);
}

// Called while dragging: returns the snapped position and shows guide lines + drop indicator (hold Alt = no snap).
QPoint WidgetManager::snap_move(DockWidget *dock, const QRect &rect)
{
	QScreen *scr = QGuiApplication::screenAt(rect.center());
	if (!scr) scr = QGuiApplication::primaryScreen();
	const QRect g = scr->availableGeometry();
	layouts::SnapResult r;
	if (!(QGuiApplication::keyboardModifiers() & Qt::AltModifier)) {
		std::vector<layouts::Rect> others;
		for (auto *d : docks) if (d != dock && d->isVisible()) others.push_back({d->x(), d->y(), d->width(), d->height()});
		r = layouts::snap_rect({rect.x(), rect.y(), rect.width(), rect.height()}, others, {g.x(), g.y(), g.width(), g.height()});
	}
	const QRect target = rect.translated(r.dx, r.dy);
	if (!guides_) guides_ = new GuideOverlay(cfg_.s("accent"));
	guides_->accent = QColor(cfg_.s("accent"));
	std::vector<std::pair<char, int>> gl;
	for (auto &gd : r.guides) gl.push_back({gd.axis, gd.pos});
	guides_->show_guides(scr->geometry(), gl, target);
	return target.topLeft();
}

void WidgetManager::end_drag(DockWidget *dock)
{
	if (guides_) guides_->hide();
	QScreen *scr = QGuiApplication::screenAt(dock->geometry().center());
	if (!scr) scr = QGuiApplication::primaryScreen();
	const QRect g = scr->availableGeometry(), r = dock->geometry();
	auto [x, y] = layouts::clamp_to({r.x(), r.y(), r.width(), r.height()}, {g.x(), g.y(), g.width(), g.height()});
	if (x != r.x() || y != r.y()) dock->move(x, y); // never leave a widget where it cannot be grabbed again
}

void WidgetManager::add(const QString &kind, const QString &name, const QString &url)
{
	const rows::Kind *k = rows::kind_of(kind.toStdString());
	if (!k) return;
	const int n = (int)docks.size() % 8;
	const QRect g = QGuiApplication::primaryScreen()->availableGeometry();
	WidgetCfg wc;
	wc.id = layouts::new_id(8);
	wc.kind = kind.toStdString();
	wc.name = name.isEmpty() ? std::string(k->title) : name.toStdString();
	wc.x = g.left() + 40 + 30 * n; wc.y = g.top() + 40 + 30 * n; wc.w = k->w; wc.h = k->h;
	for (auto &m : cfg_.list("stats_metrics")) wc.metrics.push_back(m.toStdString());
	wc.url = url.toStdString();
	make(wc);
	cfg_.set("stats_visible", true);
	commit();
	apply();
	svc_.refresh();
	emit changed();
	dirty_.start();
}

void WidgetManager::remove(const QString &id)
{
	if (DockWidget *d = find(id)) {
		docks.erase(std::find(docks.begin(), docks.end(), d));
		d->close();
		d->deleteLater();
	}
	commit();
	emit changed();
	dirty_.start();
}

void WidgetManager::apply(bool auto_add)
{
	const bool master = cfg_.b("stats_visible");
	if (master && docks.empty() && auto_add) { add("stats"); return; } // first use: give the user something to see
	bool any = false;
	for (auto *d : docks) {
		const bool vis = master && d->w.visible;
		d->setVisible(vis);
		if (vis) { d->render(); d->raise(); any = true; }
	}
	if (master) timer_.start(qMax(500, cfg_.i("stats_interval_ms")));
	else timer_.stop();
	svc_.set_widgets_visible(master && any);
}

void WidgetManager::refresh()
{
	for (auto *d : docks) if (d->isVisible()) d->render();
}

void WidgetManager::set_click_through_all(bool on) { for (auto *d : docks) d->set_click_through(on); }

void WidgetManager::toggle_click_through_all()
{
	if (docks.empty()) return;
	const bool on = std::none_of(docks.begin(), docks.end(), [](DockWidget *d) { return d->w.click_through; });
	set_click_through_all(on);
	svc_.say("info", on ? "Widgets are click-through" : "Widgets are interactive again");
}

} // namespace ovl::ui
