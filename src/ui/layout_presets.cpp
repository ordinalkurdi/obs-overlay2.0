#include "layout_presets.hpp"
#include "common.hpp"
#include <QDateTime>
#include <QDialogButtonBox>
#include <QScrollArea>

namespace ovl::ui {

using layouts::PresetError;
static QString qs(const std::string &s) { return QString::fromStdString(s); }

// Asks for a preset name with live validation (empty / too long / duplicate): focus starts in the field, Enter saves,
// Esc cancels, and Save stays disabled while the name is invalid.
PresetNameDialog::PresetNameDialog(layouts::PresetStore &store, const QString &title, const QString &text, const QString &exclude_id, const QString &ok_label)
	: QDialog(nullptr), store_(store), exclude_(exclude_id)
{
	setWindowTitle(title);
	setModal(true);
	setMinimumWidth(340);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(16, 14, 16, 14);
	lay->setSpacing(8);
	auto *lab = new QLabel("Preset name");
	lab->setObjectName("settingTitle");
	edit_ = new QLineEdit(text);
	edit_->setMaxLength(layouts::MAX_NAME);
	edit_->setPlaceholderText("For example: Streaming, Gaming, Minimal");
	edit_->setAccessibleName("Preset name");
	err_ = new QLabel;
	err_->setStyleSheet("color:#ff7b7b");
	err_->setWordWrap(true);
	err_->setMinimumHeight(18);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	ok_ = buttons->button(QDialogButtonBox::Ok);
	ok_->setText(ok_label);
	ok_->setObjectName("primary");
	ok_->setDefault(true);
	connect(buttons, &QDialogButtonBox::accepted, this, &PresetNameDialog::accept_name);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	lay->addWidget(lab); lay->addWidget(edit_); lay->addWidget(err_); lay->addWidget(buttons);
	connect(edit_, &QLineEdit::textChanged, this, [this] { check(); });
	connect(edit_, &QLineEdit::returnPressed, this, &PresetNameDialog::accept_name);
	check();
	edit_->selectAll();
	edit_->setFocus();
	prepare_dialog(this);
}

void PresetNameDialog::check()
{
	try {
		store_.validate_name(edit_->text().toStdString(), exclude_.toStdString());
		err_->setText({});
		ok_->setEnabled(true);
	} catch (const PresetError &e) {
		err_->setText(edit_->text().trimmed().isEmpty() ? QString() : QString::fromUtf8(e.what()));
		ok_->setEnabled(false);
	}
}

void PresetNameDialog::accept_name()
{
	if (!ok_->isEnabled()) return;
	result_name = edit_->text();
	accept();
}

// ---- controller -------------------------------------------------------------------------------------------------------
PresetController::PresetController(WidgetManager &m, Service &s)
	: store(s.cfg.presets(), [&s] { s.cfg.save(); }), mgr(m), svc(s)
{
	connect(&mgr, &WidgetManager::layout_changed, this, &PresetController::changed);
	connect(&mgr, &WidgetManager::changed, this, &PresetController::changed);
}

bool PresetController::is_modified() { return store.is_modified(mgr.current_layout()); }

QString PresetController::status_text()
{
	layouts::Preset *a = store.active();
	if (!a) return "No preset active";
	return "Active: " + qs(a->name) + (is_modified() ? "  (modified)" : "");
}

void PresetController::save_new()
{
	PresetNameDialog dlg(store, "Save layout as preset");
	if (dlg.exec() != QDialog::Accepted) return;
	try {
		auto &p = store.create(dlg.result_name.toStdString(), mgr.current_layout());
		svc.say("success", QString("Preset \"%1\" saved").arg(qs(p.name)));
	} catch (const PresetError &e) {
		message("Cannot save preset", QString::fromUtf8(e.what()));
		return;
	}
	emit changed();
}

void PresetController::load(const QString &qid)
{
	const std::string id = qid.toStdString();
	layouts::Preset *p = store.get(id);
	if (!p) { svc.say("error", "That preset no longer exists"); emit changed(); return; }
	layouts::Preset *a = store.active();
	if (a && a->id != id && is_modified() &&
	    !confirm("Unsaved layout changes", QString("You changed the layout since loading \"%1\".\nLoad \"%2\" anyway? The changes are not saved to \"%1\".").arg(qs(a->name), qs(p->name))))
		return;
	const layouts::Layout lay = p->layout; // copy: applying may touch the settings the preset lives in
	const QString name = qs(p->name);
	mgr.apply_layout(lay);
	store.set_active(id);
	svc.say("info", QString("Preset \"%1\" loaded").arg(name));
	emit changed();
}

void PresetController::rename(const QString &qid)
{
	const std::string id = qid.toStdString();
	layouts::Preset *p = store.get(id);
	if (!p) return;
	PresetNameDialog dlg(store, "Rename preset", qs(p->name), qid, "Rename");
	if (dlg.exec() != QDialog::Accepted) return;
	const QString old = qs(p->name);
	try {
		store.rename(id, dlg.result_name.toStdString());
	} catch (const PresetError &e) {
		message("Cannot rename preset", QString::fromUtf8(e.what()));
		return;
	}
	svc.say("success", QString("Preset \"%1\" renamed to \"%2\"").arg(old, qs(store.get(id)->name)));
	emit changed();
}

void PresetController::overwrite(const QString &qid)
{
	const std::string id = qid.toStdString();
	layouts::Preset *p = store.get(id);
	if (!p) return;
	const QString name = qs(p->name);
	if (!confirm("Update preset", QString("Replace the saved layout of \"%1\" with the current layout?").arg(name))) return;
	try {
		store.overwrite(id, mgr.current_layout());
	} catch (const PresetError &e) {
		message("Cannot update preset", QString::fromUtf8(e.what()));
		return;
	}
	svc.say("success", QString("Preset \"%1\" updated").arg(name));
	emit changed();
}

void PresetController::remove(const QString &qid)
{
	layouts::Preset *p = store.get(qid.toStdString());
	if (!p) return;
	const QString name = qs(p->name);
	if (!confirm("Delete preset", QString("Delete the preset \"%1\"? This cannot be undone.").arg(name), true)) return;
	store.remove(qid.toStdString());
	svc.say("success", QString("Preset \"%1\" deleted").arg(name));
	emit changed();
}

// ---- panel ----------------------------------------------------------------------------------------------------------------
PresetPanel::PresetPanel(PresetController &ctl, int max_h, QWidget *parent) : QWidget(parent), ctl_(ctl)
{
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->setSpacing(8);
	auto *head = new QHBoxLayout;
	auto *title = new QLabel("LAYOUT PRESETS");
	title->setObjectName("sectionTitle");
	auto *save = new QPushButton("Save current layout...");
	save->setObjectName("primary");
	save->setToolTip("Save the widgets' current arrangement as a new preset");
	connect(save, &QPushButton::clicked, &ctl_, &PresetController::save_new);
	head->addWidget(title); head->addStretch(1); head->addWidget(save);
	status_ = new QLabel;
	status_->setObjectName("dim");
	status_->setWordWrap(true);
	host_ = new QWidget;
	hl_ = new QVBoxLayout(host_);
	hl_->setContentsMargins(0, 0, 0, 0);
	hl_->setSpacing(2);
	auto *sc = new QScrollArea;
	sc->setWidgetResizable(true);
	sc->setFrameShape(QFrame::NoFrame);
	sc->setWidget(host_);
	sc->setMaximumHeight(max_h);
	sc->setMinimumHeight(70);
	lay->addLayout(head); lay->addWidget(status_); lay->addWidget(sc);
	connect(&ctl_, &PresetController::changed, this, &PresetPanel::rebuild);
	rebuild();
}

void PresetPanel::rebuild()
{
	while (QLayoutItem *it = hl_->takeAt(0)) {
		if (QWidget *w = it->widget()) { w->setParent(nullptr); w->deleteLater(); }
		delete it;
	}
	status_->setText(ctl_.status_text());
	auto &store = ctl_.store;
	if (store.presets().empty()) {
		auto *e = new QLabel("No presets yet. Arrange your widgets, then press \"Save current layout...\".", host_);
		e->setObjectName("dim");
		e->setWordWrap(true);
		hl_->addWidget(e);
	}
	const std::string active = store.active_id();
	const bool modified = ctl_.is_modified();
	const QString accent = ctl_.svc.cfg.s("accent");
	for (auto &p : store.presets()) {
		const bool is_active = p.id == active;
		const size_t n = p.layout.widgets.size();
		QString sub = (is_active ? (modified ? "Active - modified - " : "Active - ") : "") + QString("%1 widget%2").arg(n).arg(n != 1 ? "s" : "");
		if (p.updated > 0) sub += " - saved " + QDateTime::fromSecsSinceEpoch((qint64)p.updated).toString("dd MMM HH:mm");
		auto *row = new Row(is_active ? "radio-on" : "radio-off", qs(p.name), sub, false, QString("Load \"%1\" (Enter)").arg(qs(p.name)), host_);
		row->set_icon(is_active ? "radio-on" : "radio-off", is_active ? accent : FG_DIM);
		row->setAccessibleName("Preset " + qs(p.name) + (is_active ? ", active" : ""));
		const QString id = qs(p.id);
		connect(row, &Row::clicked, this, [this, id] { ctl_.load(id); });
		struct A { const char *ico, *tip; void (PresetController::*fn)(const QString &); };
		for (A a : {A{"pencil", "Rename preset", &PresetController::rename}, A{"save", "Update: overwrite this preset with the current layout", &PresetController::overwrite},
			    A{"trash", "Delete preset", &PresetController::remove}}) {
			auto *b = tool_button(a.ico, a.tip, false, 16);
			auto fn = a.fn;
			connect(b, &QToolButton::clicked, this, [this, fn, id] { (ctl_.*fn)(id); });
			row->add_tail(b);
		}
		hl_->addWidget(row);
	}
	hl_->addStretch(1);
}

} // namespace ovl::ui
