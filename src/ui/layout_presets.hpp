// Layout presets in the UI: one controller (all actions + feedback) and one reusable panel (the list), used both inside
// the overlay's Widgets page and in the layout editor toolbar, so they always show the same state.
#pragma once
#include "../core/layouts.hpp"
#include "widgets_dock.hpp"
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ovl::ui {

class PresetNameDialog : public QDialog {
	Q_OBJECT
public:
	PresetNameDialog(layouts::PresetStore &store, const QString &title, const QString &text = {}, const QString &exclude_id = {}, const QString &ok_label = "Save");
	QString result_name;
private:
	void check();
	void accept_name();
	layouts::PresetStore &store_;
	QString exclude_;
	QLineEdit *edit_;
	QLabel *err_;
	QPushButton *ok_;
};

// Every preset action lives here (save, load, rename, overwrite, delete), with confirmations and feedback. Nothing ever
// writes to a preset except an explicit Save / Update, so switching presets can never overwrite another.
class PresetController : public QObject {
	Q_OBJECT
public:
	PresetController(WidgetManager &mgr, Service &svc);
	layouts::PresetStore store;
	WidgetManager &mgr;
	Service &svc;

	bool is_modified();
	QString status_text();
	void save_new();
	void load(const QString &id);
	void rename(const QString &id);
	void overwrite(const QString &id);
	void remove(const QString &id);
signals:
	void changed();
};

// The preset list: click (or Enter) a row to load it; rename / update / delete buttons on each row.
class PresetPanel : public QWidget {
	Q_OBJECT
public:
	PresetPanel(PresetController &ctl, int max_list_height = 230, QWidget *parent = nullptr);
private:
	void rebuild();
	PresetController &ctl_;
	QLabel *status_;
	QWidget *host_;
	QVBoxLayout *hl_;
};

} // namespace ovl::ui
