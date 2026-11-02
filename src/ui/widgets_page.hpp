#pragma once
#include "layout_edit.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>

namespace ovl::ui {

// Page inside the overlay to add and configure widgets (needed because click-through widgets ignore the mouse).
class WidgetsPage : public QWidget {
	Q_OBJECT
public:
	WidgetsPage(WidgetManager &mgr, PresetController &presets, LayoutEditor &editor, QWidget *parent = nullptr);
protected:
	void showEvent(QShowEvent *) override;
private:
	void preset_changed();
	void add_web();
	void rebuild();
	QWidget *card(DockWidget *d);
	WidgetManager &mgr_;
	QCheckBox *master_;
	QComboBox *kind_, *web_preset_;
	QLineEdit *web_in_;
	QLabel *web_err_;
	QWidget *host_;
	QVBoxLayout *hl_;
};

} // namespace ovl::ui
