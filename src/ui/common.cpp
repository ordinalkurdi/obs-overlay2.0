#include "common.hpp"
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <QWindow>

namespace ovl::ui {

QFrame *separator()
{
	auto *f = new QFrame;
	f->setObjectName("sep");
	f->setFixedHeight(1);
	return f;
}

QToolButton *tool_button(const QString &icon_name, const QString &tip, bool checkable, int size)
{
	auto *b = new QToolButton;
	b->setIcon(icon(icon_name));
	b->setToolTip(tip);
	b->setAccessibleName(tip);
	b->setCheckable(checkable);
	b->setIconSize(QSize(size, size));
	b->setCursor(Qt::PointingHandCursor);
	return b;
}

// ---- Row --------------------------------------------------------------------------------------------------------
Row::Row(const QString &icon_name, const QString &title, const QString &subtitle, bool chevron, const QString &tooltip, QWidget *parent) : QFrame(parent)
{
	setObjectName("row");
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::PointingHandCursor);
	setAttribute(Qt::WA_StyledBackground, true);
	lay_ = new QHBoxLayout(this);
	lay_->setContentsMargins(12, 0, 12, 0);
	lay_->setSpacing(12);
	ico_ = new QLabel(this);
	ico_->setFixedSize(22, 22);
	set_icon(icon_name);
	auto *col = new QVBoxLayout;
	col->setSpacing(1);
	title_ = new QLabel(title, this);
	sub_ = new QLabel(subtitle, this);
	sub_->setObjectName("dim");
	sub_->setVisible(!subtitle.isEmpty());
	col->addWidget(title_);
	col->addWidget(sub_);
	lay_->addWidget(ico_);
	lay_->addLayout(col, 1);
	right_ = new QLabel(this);
	right_->setFixedSize(20, 20);
	if (chevron) right_->setPixmap(icon_pixmap("chevron-right", 18, FG_DIM));
	lay_->addWidget(right_);
	if (!tooltip.isEmpty()) setToolTip(tooltip);
}
void Row::set_icon(const QString &n, const QString &c) { ico_->setPixmap(icon_pixmap(n, 20, c)); }
void Row::set_right_icon(const QString &n, const QString &c) { right_->setPixmap(icon_pixmap(n, 18, c)); }
void Row::set_texts(const QString &t, const QString &s, bool st, bool ss)
{
	if (st) title_->setText(t);
	if (ss) set_sub(s);
}
void Row::add_tail(QWidget *w) { lay_->insertWidget(lay_->count() - 1, w); }
void Row::mouseReleaseEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit clicked();
}
void Row::keyPressEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) emit clicked();
	else QFrame::keyPressEvent(e);
}

// ---- SeekSlider ---------------------------------------------------------------------------------------------------
void SeekSlider::seek_to(int x)
{
	QStyleOptionSlider opt;
	initStyleOption(&opt);
	QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
	QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
	int span = qMax(1, groove.width() - handle.width());
	int pos = x - groove.x() - handle.width() / 2;
	int v = QStyle::sliderValueFromPosition(minimum(), maximum(), qBound(0, pos, span), span, opt.upsideDown);
	setSliderPosition(v);
	emit sliderMoved(v);
}
void SeekSlider::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton) { setSliderDown(true); seek_to((int)e->position().x()); e->accept(); }
	else QSlider::mousePressEvent(e);
}
void SeekSlider::mouseMoveEvent(QMouseEvent *e)
{
	if (isSliderDown()) { seek_to((int)e->position().x()); e->accept(); }
	else QSlider::mouseMoveEvent(e);
}
void SeekSlider::mouseReleaseEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton && isSliderDown()) { setSliderDown(false); e->accept(); }
	else QSlider::mouseReleaseEvent(e);
}

// ---- Grip -----------------------------------------------------------------------------------------------------------
Grip::Grip(QWidget *parent, Qt::Edges edges, Qt::CursorShape cursor, std::function<bool()> allowed, std::function<void()> started)
	: QWidget(parent), edges_(edges), allowed_(std::move(allowed)), started_(std::move(started))
{
	setCursor(cursor);
}
void Grip::mousePressEvent(QMouseEvent *e)
{
	if (e->button() != Qt::LeftButton) return;
	QWindow *h = window()->windowHandle();
	if (h && (!allowed_ || allowed_())) {
		if (started_) started_();
		h->startSystemResize(edges_);
	}
}

// ---- LevelBar ---------------------------------------------------------------------------------------------------------
bool LevelBar::step(double dt)
{
	bool moving = motion.step(dt);
	if (moving || motion.value == 0.0) update();
	return moving;
}
void LevelBar::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const double w = width(), h = height(), r = h / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(QColor("#2c2c2c"));
	p.drawRoundedRect(QRectF(0, 0, w, h), r, r);
	const double fill = w * motion.value / 100.0;
	if (fill > 0.5) {
		QLinearGradient g(0, 0, w, 0); // fixed across the full width, like a real meter
		g.setColorAt(0.0, "#4cc38a"); g.setColorAt(0.68, "#4cc38a"); g.setColorAt(0.85, "#e6b800"); g.setColorAt(1.0, "#ff5c5c");
		p.setBrush(g);
		p.setClipRect(QRectF(0, 0, fill, h));
		p.drawRoundedRect(QRectF(0, 0, w, h), r, r);
		p.setClipping(false);
	}
	const double pk = w * motion.peak / 100.0;
	if (pk > 3) { p.setBrush(QColor(255, 255, 255, 190)); p.drawRect(QRectF(qMin(pk, w) - 2, 0, 2, h)); }
}

// ---- dialogs ------------------------------------------------------------------------------------------------------------
static std::function<QScreen *()> g_screen;
void set_screen_provider(std::function<QScreen *()> fn) { g_screen = std::move(fn); }
QScreen *target_screen()
{
	QScreen *s = g_screen ? g_screen() : nullptr;
	return s ? s : QGuiApplication::primaryScreen();
}

void center_widget(QWidget *w)
{
	const QRect g = target_screen()->availableGeometry();
	w->adjustSize();
	const int width = qMin(w->width(), g.width()), height = qMin(w->height(), g.height());
	if (width != w->width() || height != w->height()) w->resize(width, height);
	w->move(qMax(g.left(), qMin(g.center().x() - width / 2, g.right() - width + 1)),
		qMax(g.top(), qMin(g.center().y() - height / 2, g.bottom() - height + 1)));
}

void prepare_dialog(QDialog *d)
{
	apply_style(d);
	d->setWindowFlag(Qt::WindowStaysOnTopHint, true); // stay above games and the overlay
	center_widget(d);
}

bool confirm(const QString &title, const QString &text, bool danger)
{
	QMessageBox box(danger ? QMessageBox::Warning : QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, nullptr);
	box.setDefaultButton(danger ? QMessageBox::No : QMessageBox::Yes);
	prepare_dialog(&box);
	return box.exec() == QMessageBox::Yes;
}

void message(const QString &title, const QString &text, bool error)
{
	QMessageBox box(error ? QMessageBox::Critical : QMessageBox::Warning, title, text, QMessageBox::Ok, nullptr);
	prepare_dialog(&box);
	box.exec();
}

NotificationWindow::NotificationWindow()
	: QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput),
	  anim_(this, "windowOpacity")
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	lab_ = new QLabel;
	lab_->setWordWrap(true);
	lab_->setAlignment(Qt::AlignCenter);
	lab_->setMaximumWidth(460);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->addWidget(lab_);
	timer_.setSingleShot(true);
	connect(&timer_, &QTimer::timeout, this, [this] { anim_.setStartValue(1.0); anim_.setEndValue(0.0); anim_.start(); });
	anim_.setDuration(350);
	connect(&anim_, &QPropertyAnimation::finished, this, [this] { if (windowOpacity() < 0.05) hide(); });
}

void NotificationWindow::show_msg(const QString &level, const QString &text, double seconds)
{
	static const QHash<QString, QString> colors = {{"info", "#3a3a3a"}, {"success", "#1f5a3d"}, {"warn", "#6b5410"}, {"error", "#7a2323"}};
	lab_->setStyleSheet(QString("background:%1; color:#f2f2f2; border-radius:8px; padding:10px 16px; font-size:14px;").arg(colors.value(level, "#3a3a3a")));
	lab_->setText(text);
	anim_.stop();
	setWindowOpacity(1.0);
	center_widget(this);
	show();
	raise();
	timer_.start((int)(seconds * 1000));
}

} // namespace ovl::ui
