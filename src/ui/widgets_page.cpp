#include "widgets_page.hpp"
#include "../core/weburl.hpp"
#include "../core/widget_rows.hpp"
#include "common.hpp"
#include <QScrollArea>
#include <QSlider>

namespace ovl::ui {

static QString qs(const std::string &s) { return QString::fromStdString(s); }

WidgetsPage::WidgetsPage(WidgetManager &mgr, PresetController &presets, LayoutEditor &editor, QWidget *parent) : QWidget(parent), mgr_(mgr)
{
	setObjectName("plain");
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(12, 10, 12, 10);
	master_ = new QCheckBox("Show floating widgets");
	master_->setChecked(mgr.service().cfg.b("stats_visible"));
	connect(master_, &QCheckBox::toggled, this, [this](bool on) {
		mgr_.service().cfg.set("stats_visible", on);
		mgr_.service().cfg.save();
		mgr_.apply();
		mgr_.service().refresh();
	});
	auto *top = new QHBoxLayout;
	kind_ = new QComboBox;
	for (auto &k : rows::kinds()) if (std::string(k.key) != "web") kind_->addItem(k.title, QString(k.key));
	auto *add = new QPushButton("Add widget");
	add->setObjectName("primary");
	connect(add, &QPushButton::clicked, this, [this] { mgr_.add(kind_->currentData().toString()); });
	top->addWidget(kind_, 1);
	top->addWidget(add);

	auto *edit = new QPushButton;
	auto sync_edit = [this, edit](bool on) {
		edit->setIcon(icon(on ? "check" : "move"));
		edit->setText(on ? " Exit layout editing" : " Edit layout...");
		edit->setToolTip(on ? "Leave edit mode" : "Arrange widgets on screen with snapping guides, and use presets");
	};
	connect(edit, &QPushButton::clicked, &editor, &LayoutEditor::toggle); // the same button starts and ends editing
	connect(&mgr, &WidgetManager::edit_mode_changed, edit, sync_edit);
	sync_edit(mgr.edit_mode);

	auto *preset_card = new QFrame;
	preset_card->setObjectName("card");
	auto *pc = new QVBoxLayout(preset_card);
	pc->setContentsMargins(10, 8, 10, 10);
	pc->addWidget(new PresetPanel(presets, 150, preset_card));

	auto *tip = new QLabel("Drag a widget to move it, drag its edges to resize (text scales with width), right-click for quick options. "
			       "Click-through widgets ignore the mouse: turn it off here, from the tray menu, or with the click-through hotkey.");
	tip->setWordWrap(true);
	tip->setObjectName("dim");
	host_ = new QWidget;
	hl_ = new QVBoxLayout(host_);
	hl_->setContentsMargins(0, 0, 0, 0);
	hl_->setSpacing(8);
	auto *sc = new QScrollArea;
	sc->setWidgetResizable(true);
	sc->setFrameShape(QFrame::NoFrame);
	sc->setWidget(host_);

	auto *webl = new QLabel("<b>Web links</b> - chat, Twitch Stream Manager, or any page");
	webl->setWordWrap(true);
	auto *wrow = new QHBoxLayout;
	web_preset_ = new QComboBox;
	for (auto &p : web::presets()) web_preset_->addItem(p.name);
	web_in_ = new QLineEdit;
	auto *wadd = new QPushButton("Add web link");
	wadd->setObjectName("primary");
	web_err_ = new QLabel;
	web_err_->setStyleSheet("color:#ff7b7b");
	web_err_->setWordWrap(true);
	wrow->addWidget(web_preset_);
	wrow->addWidget(web_in_, 1);
	wrow->addWidget(wadd);
	connect(web_preset_, &QComboBox::currentIndexChanged, this, [this] { preset_changed(); });
	connect(wadd, &QPushButton::clicked, this, &WidgetsPage::add_web);
	connect(web_in_, &QLineEdit::returnPressed, this, &WidgetsPage::add_web);
	preset_changed();

	lay->addWidget(master_);
	lay->addWidget(edit);
	lay->addWidget(preset_card);
	lay->addLayout(top);
	lay->addWidget(webl);
	lay->addLayout(wrow);
	lay->addWidget(web_err_);
	lay->addWidget(tip);
	lay->addWidget(sc, 1);
	connect(&mgr, &WidgetManager::changed, this, &WidgetsPage::rebuild);
	rebuild();
}

void WidgetsPage::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	QSignalBlocker b(master_);
	master_->setChecked(mgr_.service().cfg.b("stats_visible"));
	rebuild();
}

void WidgetsPage::preset_changed()
{
	const auto &p = web::presets()[(size_t)web_preset_->currentIndex()];
	web_in_->setVisible(p.input != nullptr);
	web_in_->setPlaceholderText(p.input ? p.input : "");
}

void WidgetsPage::add_web()
{
	const auto &p = web::presets()[(size_t)web_preset_->currentIndex()];
	auto url = web::preset_url(p.name, web_in_->text().toStdString());
	if (!url) { web_err_->setText(p.error); return; }
	web_err_->setText({});
	mgr_.add("web", std::string(p.name) == "Custom URL" ? qs(web::host_of(*url)) : QString(p.name), qs(*url));
	web_in_->clear();
}

void WidgetsPage::rebuild()
{
	while (QLayoutItem *it = hl_->takeAt(0)) {
		if (QWidget *w = it->widget()) w->deleteLater();
		delete it;
	}
	if (mgr_.docks.empty()) {
		auto *l = new QLabel("No widgets yet. Pick a type above and press Add widget.");
		l->setObjectName("dim");
		hl_->addWidget(l);
	}
	for (auto *d : mgr_.docks) hl_->addWidget(card(d));
	hl_->addStretch();
}

QWidget *WidgetsPage::card(DockWidget *d)
{
	auto *c = new QFrame;
	c->setObjectName("card");
	c->setStyleSheet("QFrame#card{background:#222; border-radius:8px;}");
	auto *v = new QVBoxLayout(c);
	v->setContentsMargins(10, 8, 10, 8);
	const QString id = qs(d->w.id);
	auto *ne = new QLineEdit(d->w.name.empty() ? QString(rows::kind_of(d->w.kind)->title) : qs(d->w.name));
	ne->setToolTip("Widget name - click to rename");
	ne->setStyleSheet("font-weight:600; background:transparent;");
	connect(ne, &QLineEdit::editingFinished, this, [this, id, ne] {
		if (DockWidget *dd = mgr_.find(id)) { dd->set_name(ne->text()); ne->setText(qs(dd->w.name)); }
	});
	v->addWidget(ne);
	if (d->w.kind == "web") {
		auto *ur = new QHBoxLayout;
		auto *ue = new QLineEdit(qs(d->w.url));
		auto *go = new QPushButton("Go");
		auto apply_url = [this, id, ue] {
			auto u = web::normalize_url(ue->text().toStdString());
			if (u) { ue->setStyleSheet({}); ue->setText(qs(*u)); if (auto *dd = mgr_.find(id)) dd->set_url(qs(*u)); }
			else ue->setStyleSheet("border:1px solid #ff7b7b;");
		};
		connect(go, &QPushButton::clicked, this, apply_url);
		connect(ue, &QLineEdit::returnPressed, this, apply_url);
		ur->addWidget(ue, 1);
		ur->addWidget(go);
		v->addLayout(ur);
	}
	auto *r1 = new QHBoxLayout;
	struct Flag { const char *text; bool on; void (DockWidget::*fn)(bool); };
	for (Flag f : {Flag{"Visible", d->w.visible, &DockWidget::set_visible_flag}, Flag{"Pinned", d->w.pinned, &DockWidget::set_pinned},
		       Flag{"Click-through", d->w.click_through, &DockWidget::set_click_through}}) {
		auto *cb = new QCheckBox(f.text);
		cb->setChecked(f.on);
		auto fn = f.fn;
		connect(cb, &QCheckBox::toggled, this, [this, id, fn](bool on) { if (auto *dd = mgr_.find(id)) (dd->*fn)(on); });
		r1->addWidget(cb);
	}
	v->addLayout(r1);
	auto *r2 = new QHBoxLayout;
	r2->addWidget(new QLabel("Opacity"));
	auto *sl = new QSlider(Qt::Horizontal);
	sl->setRange(20, 100);
	sl->setValue((int)std::lround(d->w.opacity * 100));
	connect(sl, &QSlider::valueChanged, this, [this, id](int val) { if (auto *dd = mgr_.find(id)) dd->set_opacity(val / 100.0); });
	r2->addWidget(sl, 1);
	v->addLayout(r2);
	auto *r3 = new QHBoxLayout;
	if (d->w.kind == "stats") {
		auto *b = new QPushButton("Metrics...");
		auto *menu = new QMenu(b);
		apply_style(menu);
		for (auto &[k, label] : rows::metrics()) {
			QAction *a = menu->addAction(qs(label));
			a->setCheckable(true);
			a->setChecked(std::find(d->w.metrics.begin(), d->w.metrics.end(), k) != d->w.metrics.end());
			const std::string key = k;
			connect(a, &QAction::toggled, this, [this, id, key](bool on) { if (auto *dd = mgr_.find(id)) dd->toggle_metric(key, on); });
		}
		b->setMenu(menu);
		r3->addWidget(b);
	}
	auto *center = new QPushButton("Center");
	connect(center, &QPushButton::clicked, this, [this, id] { if (auto *dd = mgr_.find(id)) dd->center_on_screen(); });
	auto *rm = new QPushButton("Remove");
	rm->setObjectName("danger");
	connect(rm, &QPushButton::clicked, this, [this, id] { mgr_.remove(id); });
	r3->addStretch();
	r3->addWidget(center);
	r3->addWidget(rm);
	v->addLayout(r3);
	return c;
}

} // namespace ovl::ui
