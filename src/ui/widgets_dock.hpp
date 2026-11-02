// Floating widgets ("docks"): add as many as you like; each can be moved, resized (text scales), made transparent,
// pinned (locked) and click-through. Managed from the overlay's Widgets page or by right-click.
#pragma once
#include "../core/layouts.hpp"
#include "../core/service.hpp"
#include "edit_overlays.hpp"
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QMenu>
#include <QPropertyAnimation>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <map>
#include <vector>

namespace ovl::ui {

class WidgetManager;
class DockWidget;

class HoverBar : public QFrame {
	Q_OBJECT
public:
	explicit HoverBar(DockWidget *dock);
	void show_opacity(double op);
	QSlider *opacity;
	QToolButton *move_btn, *pin, *ct, *menu_btn, *close_btn;
};

class DockWidget : public QWidget {
	Q_OBJECT
public:
	DockWidget(Service &svc, const layouts::WidgetCfg &cfg);
	layouts::WidgetCfg w;
	WidgetManager *mgr = nullptr;
	bool pinned = false;

	void render();
	void set_edit_mode(bool on);
	void apply_config(const layouts::WidgetCfg &n, bool animate);
	void set_name(const QString &name);
	void set_pinned(bool on);
	void set_click_through(bool on);
	void set_opacity(double v, bool commit = true);
	void set_visible_flag(bool on);
	void toggle_metric(const std::string &key, bool on);
	void set_url(const QString &url);
	void center_on_screen();
	void drag_start(const QPoint &gp);
	void drag_move(const QPoint &gp);
	void drag_end();
	void show_menu(const QPoint &pos) { menu(pos); }

signals:
	void needs_save();
	void flags_changed();
	void remove_requested(const QString &id);
	void geometry_changed();

protected:
	void resizeEvent(QResizeEvent *) override;
	void moveEvent(QMoveEvent *) override;
	void enterEvent(QEnterEvent *) override;
	void leaveEvent(QEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void contextMenuEvent(QContextMenuEvent *) override;

private:
	static Qt::WindowFlags flags_for(bool click_through);
	bool ct_now() const { return w.click_through && !edit_; }
	void place_grips();
	void sync_pin_ui();
	void refresh_flags();
	void apply_opacity();
	void save_geometry();
	void menu(const QPoint &pos);
	void build_web_card();

	Service &svc_;
	QLabel *lab_ = nullptr;
	QWidget *web_card_ = nullptr;
	QLabel *web_lab_ = nullptr;
	QGraphicsOpacityEffect *fx_ = nullptr;
	EditFrame *edit_frame_ = nullptr;
	HoverBar *hover_ = nullptr;
	std::map<std::string, QWidget *> grips_;
	QTimer persist_;
	QPropertyAnimation *anim_ = nullptr;
	QString style_;
	bool edit_ = false, dragging_ = false;
	QPoint drag_origin_, drag_pos_;
};

class WidgetManager : public QObject {
	Q_OBJECT
public:
	explicit WidgetManager(Service &svc, QObject *parent = nullptr);
	~WidgetManager() override;

	std::vector<DockWidget *> docks; // in layout order
	bool edit_mode = false;

	Service &service() { return svc_; }
	DockWidget *find(const QString &id) const;
	layouts::Layout current_layout() const;
	void apply_layout(const layouts::Layout &layout, bool animate = true);
	void reset_default_layout();
	void hide_all();
	void show_all();
	bool all_hidden() const;
	void set_edit_mode(bool on);
	QPoint snap_move(DockWidget *dock, const QRect &rect);
	void end_drag(DockWidget *dock);
	void add(const QString &kind, const QString &name = {}, const QString &url = {});
	void remove(const QString &id);
	void apply(bool auto_add = true); // show / hide according to the master switch
	void refresh();                   // re-render visible widgets
	void set_click_through_all(bool on);
	void toggle_click_through_all();
	void commit();                    // widgets -> settings file

signals:
	void changed();        // widgets added / removed or their flags changed (pages should rebuild)
	void layout_changed(); // anything about the arrangement changed (debounced): drives the preset "modified" state
	void edit_mode_changed(bool on);

private:
	DockWidget *make(const layouts::WidgetCfg &wc);
	static void keep_on_screen(layouts::WidgetCfg &wc);
	Service &svc_;
	Settings &cfg_;
	GuideOverlay *guides_ = nullptr;
	QTimer dirty_, timer_;
};

// Opens OBS's own "Custom Browser Docks" dialog (the supported way to embed web pages inside OBS).
void open_obs_browser_docks(const QString &url);

} // namespace ovl::ui
