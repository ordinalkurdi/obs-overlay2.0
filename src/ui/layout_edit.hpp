// Layout edit mode, modelled on Discord's overlay layout editor: every widget gets a dashed outline and a small chip
// under it with its name; dragging shows snap guides and a drop indicator; a toolbar at the top of the screen offers
// Widgets / Hide All / Reset Default Layout / Presets / Save / Done.
#pragma once
#include "layout_presets.hpp"
#include <QMenu>
#include <map>

namespace ovl::ui {

class LayoutEditor;

class EditChip : public QWidget {
public:
	explicit EditChip(DockWidget *dock);
	void sync(bool editing);
	void reposition();
private:
	DockWidget *dock_;
	QLabel *name_;
};

class PresetFlyout : public QWidget {
public:
	explicit PresetFlyout(PresetController &ctl);
};

class LayoutToolbar : public QWidget {
public:
	explicit LayoutToolbar(LayoutEditor &ed);
	void refresh();
	void place();
protected:
	void hideEvent(QHideEvent *) override;
private:
	void place_flyout();
	void fill_menu();
	LayoutEditor &ed_;
	QPushButton *widgets_btn_, *hide_btn_, *reset_btn_, *preset_btn_, *save_btn_;
	QMenu *menu_;
};

// Owns the edit-mode UI (toolbar, chips, preset flyout). The mode itself lives on the WidgetManager.
class LayoutEditor : public QObject {
	Q_OBJECT
public:
	LayoutEditor(WidgetManager &mgr, PresetController &ctl);
	~LayoutEditor() override;
	WidgetManager &mgr;
	PresetController &ctl;
	PresetFlyout *flyout;
	LayoutToolbar *toolbar;
	bool active() const { return mgr.edit_mode; }
	void start() { mgr.set_edit_mode(true); }
	void stop() { mgr.set_edit_mode(false); }
	void toggle() { mgr.set_edit_mode(!mgr.edit_mode); }
	void sync();
private:
	void mode(bool on);
	std::map<std::string, EditChip *> chips_;
};

} // namespace ovl::ui
