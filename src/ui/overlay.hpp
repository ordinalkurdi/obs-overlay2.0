// The slide-out panel: docks to the edge of a monitor, opens with a hotkey, the tray icon or by hovering the screen
// edge, and controls OBS without leaving the game.
#pragma once
#include "../core/gallery.hpp"
#include "../core/service.hpp"
#include "common.hpp"
#include "gallery_view.hpp"
#include <QComboBox>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <map>

class QScreen;

namespace ovl::ui {

// Nearly invisible 3px strip on the monitor edge; hovering it slides the panel out.
class EdgeTrigger : public QWidget {
	Q_OBJECT
public:
	EdgeTrigger();
	void configure(QScreen *screen, const QString &side, bool enabled, int y, int h);
	void refresh(bool panel_open) { setVisible(enabled_ && !panel_open); }
signals:
	void entered();
protected:
	void paintEvent(QPaintEvent *) override;
	void enterEvent(QEnterEvent *) override { emit entered(); }
private:
	bool enabled_ = false;
};

class Overlay : public QWidget {
	Q_OBJECT
public:
	Overlay(Service &svc, MediaStore &store);
	~Overlay() override;

	bool pinned = false;
	bool user_resize = false;
	QStackedWidget *stack;
	GalleryStrip *strip;
	Row *gal_row, *stat_row;

	QScreen *screen() const;
	bool is_open() const { return open_ && isVisible(); }

	void show_panel();
	void hide_panel();
	void toggle_visible() { is_open() ? hide_panel() : show_panel(); }
	void apply_settings();
	void sync();
	void rec_clicked();
	void live_clicked();
	void set_gallery(QWidget *w);
	void set_widgets_page(QWidget *w);
	void set_settings_page(QWidget *w);
	void drop_settings(bool go_home = true);
	void show_page(const QString &name);

signals:
	void open_settings();
	void visibility_changed(bool visible);

protected:
	void resizeEvent(QResizeEvent *) override;
	void hideEvent(QHideEvent *) override;
	void showEvent(QShowEvent *) override;
	void leaveEvent(QEvent *) override;
	void enterEvent(QEnterEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	struct MixRow {
		QToolButton *mute;
		QLabel *dot, *name;
		LevelBar *bar;
		QSlider *slider;
		double active_until = 0;
		bool lit = false;
		int inactive = -1;
	};

	void build();
	bool docked() const { return cfg_.s("dock_mode") == "docked"; }
	QRect rect_for(bool shown) const;
	void clip();
	void edge_cfg();
	void place_grips();
	void persist_geometry();
	void pin(bool on);
	void bump_idle();
	void auto_hide();
	void maybe_leave();
	bool confirm_if(const char *key, const QString &text);
	void restore_sections();
	void remember(const char *key, bool v);
	void toggle_sources();
	void toggle_mixer();
	void set_sources(const std::vector<ItemInfo> &items);
	QString rec_sub() const;
	QString stream_sub() const;
	void mixer(const std::vector<InputInfo> &inputs);
	void update_meters();
	void meter_tick();

	Service &svc_;
	Settings &cfg_;
	MediaStore &store_;
	bool open_ = false;
	QFrame *panel_, *header_;
	QLabel *title_, *dot_, *footer_;
	QToolButton *pin_btn_, *back_;
	QWidget *srcbox_, *mixer_box_;
	QVBoxLayout *sl_, *ml_;
	Row *rec_, *rep_, *save_, *shot_, *live_, *pause_, *vcam_, *scene_row_, *src_row_, *mix_row_;
	QComboBox *scene_box_;
	QWidget *gallery_ = nullptr, *settings_ = nullptr, *widgets_ = nullptr;
	NotificationWindow *toast_;
	EdgeTrigger *edge_;
	std::map<QString, Grip *> grips_;
	QPropertyAnimation *anim_;
	QTimer idle_, leave_timer_, persist_, meter_timer_;
	QElapsedTimer clock_;
	double meter_t_ = 0;
	bool top_key_known_ = false, top_key_ = false;
	QPoint drag_;
	bool dragging_ = false;
	std::map<QString, MixRow> rows_;
	QString mix_sig_, src_sig_;
};

} // namespace ovl::ui
