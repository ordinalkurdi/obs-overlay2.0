// Small reusable pieces: rows, buttons, sliders, resize grips, level bars, dialogs and the centered notification.
#pragma once
#include "../core/audio_math.hpp"
#include "style.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <functional>

class QScreen;

namespace ovl::ui {

QFrame *separator();
QToolButton *tool_button(const QString &icon_name, const QString &tip, bool checkable = false, int size = 18);

// Compact NVIDIA-style row: icon, title + dim subtitle, optional right-hand widget or chevron.
class Row : public QFrame {
	Q_OBJECT
public:
	Row(const QString &icon_name, const QString &title, const QString &subtitle = {}, bool chevron = true, const QString &tooltip = {}, QWidget *parent = nullptr);
	void set_icon(const QString &name, const QString &color = FG);
	void set_right_icon(const QString &name, const QString &color = FG);
	void set_texts(const QString &title = {}, const QString &sub = {}, bool set_title = false, bool set_sub = false);
	void set_title(const QString &t) { title_->setText(t); }
	void set_sub(const QString &t) { sub_->setText(t); sub_->setVisible(!t.isEmpty()); }
	void add_tail(QWidget *w);
signals:
	void clicked();
protected:
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
private:
	QLabel *ico_, *title_, *sub_, *right_;
	QHBoxLayout *lay_;
};

// A slider that jumps straight to the clicked position instead of paging.
class SeekSlider : public QSlider {
	Q_OBJECT
public:
	using QSlider::QSlider;
protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
private:
	void seek_to(int x);
};

// Invisible resize handle on a window border; uses the OS resize so it feels native.
class Grip : public QWidget {
public:
	Grip(QWidget *parent, Qt::Edges edges, Qt::CursorShape cursor, std::function<bool()> allowed, std::function<void()> started = {});
protected:
	void mousePressEvent(QMouseEvent *e) override;
private:
	Qt::Edges edges_;
	std::function<bool()> allowed_;
	std::function<void()> started_;
};

// Smooth OBS-style level bar (green / yellow / red) with a peak-hold marker, animated by the overlay's meter timer.
class LevelBar : public QWidget {
public:
	LevelBar() { setFixedHeight(6); }
	void set_target(double pct) { motion.target = qBound(0.0, pct, 100.0); }
	bool step(double dt);
	audio::Ballistics motion;
protected:
	void paintEvent(QPaintEvent *) override;
};

// ---- dialogs: always centered on the screen the overlay lives on, above games ------------------------------------
void set_screen_provider(std::function<QScreen *()> fn);
QScreen *target_screen();
void center_widget(QWidget *w);
void prepare_dialog(QDialog *d);
bool confirm(const QString &title, const QString &text, bool danger = false);
void message(const QString &title, const QString &text, bool error = false);

// Notification in the exact centre of the screen: click-through and non-focusing, so it never blocks a game.
class NotificationWindow : public QWidget {
	Q_OBJECT
public:
	NotificationWindow();
	void show_msg(const QString &level, const QString &text, double seconds);
private:
	QLabel *lab_;
	QTimer timer_;
	QPropertyAnimation anim_;
};

} // namespace ovl::ui
