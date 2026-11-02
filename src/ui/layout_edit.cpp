#include "layout_edit.hpp"
#include "../core/weburl.hpp"
#include "../core/widget_rows.hpp"
#include "common.hpp"
#include <QGuiApplication>
#include <QInputDialog>
#include <QScreen>
#include <QShortcut>

namespace ovl::ui {

static const Qt::WindowFlags TOP = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
static QString qs(const std::string &s) { return QString::fromStdString(s); }

// ---- chip ---------------------------------------------------------------------------------------------------------------------
EditChip::EditChip(DockWidget *d) : QWidget(nullptr, TOP | Qt::WindowDoesNotAcceptFocus), dock_(d)
{
	apply_style(this);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	auto *outer = new QHBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *frame = new QFrame;
	frame->setObjectName("chip");
	frame->setStyleSheet("#chip{background:rgba(30,30,30,240);border:1px solid #3a3a3a;border-radius:12px;}");
	outer->addWidget(frame);
	auto *h = new QHBoxLayout(frame);
	h->setContentsMargins(10, 3, 10, 3);
	name_ = new QLabel;
	name_->setStyleSheet("font-weight:600;");
	h->addWidget(name_);
}

void EditChip::sync(bool editing)
{
	name_->setText(dock_->w.name.empty() ? QString(rows::kind_of(dock_->w.kind)->title) : qs(dock_->w.name));
	adjustSize();
	const bool show = editing && dock_->isVisible();
	if (show) reposition();
	setVisible(show);
	if (show) raise();
}

void EditChip::reposition()
{
	const QRect g = dock_->geometry();
	QScreen *s = QGuiApplication::screenAt(g.center());
	const QRect sg = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
	const int x = qMax(sg.left(), qMin(g.left(), sg.right() - width() + 1));
	int y = g.bottom() + 6;
	if (y + height() > sg.bottom()) y = g.top() - height() - 6; // no room below: put it above
	if (y < sg.top()) y = g.bottom() - height() - 6;            // no room above either: inside
	move(x, y);
}

// ---- flyout ----------------------------------------------------------------------------------------------------------------------
PresetFlyout::PresetFlyout(PresetController &ctl) : QWidget(nullptr, TOP)
{
	apply_style(this);
	setAttribute(Qt::WA_TranslucentBackground);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *frame = new QFrame;
	frame->setObjectName("flyout");
	frame->setStyleSheet("#flyout{background:rgba(24,24,24,244);border:1px solid #3a3a3a;border-radius:12px;}");
	outer->addWidget(frame);
	auto *v = new QVBoxLayout(frame);
	v->setContentsMargins(12, 10, 12, 12);
	v->addWidget(new PresetPanel(ctl, 300, frame));
	setFixedWidth(400);
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { hide(); });
}

// ---- toolbar -----------------------------------------------------------------------------------------------------------------------
LayoutToolbar::LayoutToolbar(LayoutEditor &ed) : QWidget(nullptr, TOP), ed_(ed)
{
	apply_style(this);
	setAttribute(Qt::WA_TranslucentBackground);
	setWindowTitle("Edit widget layout");
	auto *outer = new QHBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *bar = new QFrame;
	bar->setObjectName("editbar");
	bar->setStyleSheet("#editbar{background:rgba(24,24,24,238);border:1px solid #3a3a3a;border-radius:12px;}");
	outer->addWidget(bar);
	auto *h = new QHBoxLayout(bar);
	h->setContentsMargins(8, 6, 8, 6);
	h->setSpacing(6);
	widgets_btn_ = new QPushButton(icon("dashboard"), " Widgets");
	widgets_btn_->setToolTip("Add a widget, or show / hide the widgets you have");
	menu_ = new QMenu(widgets_btn_);
	apply_style(menu_);
	connect(menu_, &QMenu::aboutToShow, this, [this] { fill_menu(); });
	widgets_btn_->setMenu(menu_);
	url_btn_ = new QPushButton(icon("globe"), " Add Custom URL");
	url_btn_->setToolTip("Add a floating web link widget for any address (chat, dashboard, ...)");
	connect(url_btn_, &QPushButton::clicked, this, &LayoutToolbar::add_custom_url);
	hide_btn_ = new QPushButton(icon("eye-off"), " Hide All");
	hide_btn_->setToolTip("Hide every widget (or show them all again)");
	connect(hide_btn_, &QPushButton::clicked, this, [this] { auto &m = ed_.mgr; m.all_hidden() ? m.show_all() : m.hide_all(); });
	reset_btn_ = new QPushButton(icon("restore"), " Reset Default Layout");
	reset_btn_->setToolTip("Arrange all widgets in the default layout (stacked along the right edge)");
	connect(reset_btn_, &QPushButton::clicked, this, [this] {
		if (confirm("Reset default layout", "Arrange all widgets in the default layout?\nPresets are not changed.")) ed_.mgr.reset_default_layout();
	});
	preset_btn_ = new QPushButton(icon("bookmark"), " Presets");
	preset_btn_->setCheckable(true);
	preset_btn_->setToolTip("Show saved layout presets");
	connect(preset_btn_, &QPushButton::toggled, this, [this](bool on) {
		if (on) { place_flyout(); ed_.flyout->show(); ed_.flyout->raise(); }
		else ed_.flyout->hide();
	});
	save_btn_ = new QPushButton(icon("save"), " Save");
	save_btn_->setToolTip("Save the current layout as a new preset");
	connect(save_btn_, &QPushButton::clicked, &ed_.ctl, &PresetController::save_new);
	auto *done = tool_button("close", "Done editing (Esc)", false, 18);
	connect(done, &QToolButton::clicked, &ed_, &LayoutEditor::stop);
	for (QWidget *b : std::initializer_list<QWidget *>{widgets_btn_, url_btn_, hide_btn_, reset_btn_, preset_btn_, save_btn_, done}) h->addWidget(b);
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { ed_.stop(); });
	connect(&ed_.ctl, &PresetController::changed, this, [this] { refresh(); });
	connect(&ed_.mgr, &WidgetManager::changed, this, [this] { refresh(); });
}

void LayoutToolbar::refresh()
{
	auto &ctl = ed_.ctl;
	layouts::Preset *a = ctl.store.active();
	QString label = " Presets";
	if (a) {
		QString n = qs(a->name);
		label = " " + n.left(18) + (n.size() > 18 ? "..." : "") + (ctl.is_modified() ? " *" : "");
	}
	preset_btn_->setText(label);
	preset_btn_->setToolTip(ctl.status_text() + " - click to manage presets");
	const bool hidden = ed_.mgr.all_hidden();
	hide_btn_->setText(hidden ? " Show All" : " Hide All");
	hide_btn_->setIcon(icon(hidden ? "eye" : "eye-off"));
}

void LayoutToolbar::place()
{
	adjustSize();
	const QRect g = target_screen()->availableGeometry();
	move(g.right() - width() - 12, g.top() + 12);
	if (ed_.flyout->isVisible()) place_flyout();
}

void LayoutToolbar::place_flyout()
{
	PresetFlyout *f = ed_.flyout;
	f->adjustSize();
	f->move(x() + width() - f->width(), y() + height() + 6);
}

void LayoutToolbar::fill_menu()
{
	auto &mgr = ed_.mgr;
	menu_->clear();
	QMenu *add = menu_->addMenu(icon("plus"), "Add widget");
	for (auto &k : rows::kinds()) {
		if (std::string(k.key) == "web") continue;
		const QString key = k.key;
		add->addAction(k.title, this, [&mgr, key] { mgr.add(key); });
	}
	menu_->addAction(icon("globe"), "Add custom URL...", this, &LayoutToolbar::add_custom_url);
	menu_->addSeparator();
	if (mgr.docks.empty()) menu_->addAction("No widgets yet")->setEnabled(false);
	for (auto *d : mgr.docks) {
		QAction *a = menu_->addAction(d->w.name.empty() ? QString(rows::kind_of(d->w.kind)->title) : qs(d->w.name));
		a->setCheckable(true);
		a->setChecked(d->w.visible);
		connect(a, &QAction::toggled, d, &DockWidget::set_visible_flag);
	}
}

// Asks for an address (re-asking until it is a valid http/https URL or the user cancels) and adds it as a web widget.
void LayoutToolbar::add_custom_url()
{
	QString text, error;
	for (;;) {
		QInputDialog dlg(nullptr);
		dlg.setWindowTitle("Add Custom URL");
		dlg.setLabelText(error.isEmpty() ? "Web address (https://...)" : error);
		dlg.setTextValue(text);
		prepare_dialog(&dlg);
		if (dlg.exec() != QDialog::Accepted) return;
		text = dlg.textValue();
		if (auto url = web::normalize_url(text.toStdString())) {
			ed_.mgr.add("web", qs(web::host_of(*url)), qs(*url));
			return;
		}
		error = "Enter a valid http(s) address, for example https://example.com/page";
	}
}

void LayoutToolbar::hideEvent(QHideEvent *e)
{
	preset_btn_->setChecked(false);
	QWidget::hideEvent(e);
}

// ---- editor ----------------------------------------------------------------------------------------------------------------------------
LayoutEditor::LayoutEditor(WidgetManager &m, PresetController &c) : mgr(m), ctl(c)
{
	flyout = new PresetFlyout(ctl);
	toolbar = new LayoutToolbar(*this);
	connect(&mgr, &WidgetManager::edit_mode_changed, this, &LayoutEditor::mode);
	connect(&mgr, &WidgetManager::changed, this, &LayoutEditor::sync);
	connect(&mgr, &WidgetManager::layout_changed, this, &LayoutEditor::sync);
}

LayoutEditor::~LayoutEditor()
{
	for (auto &kv : chips_) delete kv.second;
	delete flyout;
	delete toolbar;
}

void LayoutEditor::mode(bool on)
{
	if (on) {
		toolbar->place(); toolbar->show(); toolbar->raise(); toolbar->activateWindow();
		sync();
	} else {
		flyout->hide();
		toolbar->hide();
		for (auto &kv : chips_) kv.second->hide();
	}
}

void LayoutEditor::sync()
{
	if (!active()) return;
	for (auto it = chips_.begin(); it != chips_.end();) {
		if (!mgr.find(qs(it->first))) { it->second->hide(); delete it->second; it = chips_.erase(it); }
		else ++it;
	}
	for (auto *d : mgr.docks) {
		EditChip *&c = chips_[d->w.id];
		if (!c) {
			c = new EditChip(d);
			connect(d, &DockWidget::geometry_changed, c, [chip = c] { chip->reposition(); });
		}
		c->sync(true);
	}
	toolbar->refresh();
}

} // namespace ovl::ui
