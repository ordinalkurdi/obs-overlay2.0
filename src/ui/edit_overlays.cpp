#include "edit_overlays.hpp"
#include <QPainter>
#include <QPen>

namespace ovl::ui {

GuideOverlay::GuideOverlay(const QString &a)
	: QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput), accent(a)
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
}

void GuideOverlay::show_guides(const QRect &sg, const std::vector<std::pair<char, int>> &guides, const QRect &target)
{
	if (geometry() != sg) setGeometry(sg);
	lines_.clear();
	for (auto &[o, p] : guides) lines_.push_back({o, p - (o == 'v' ? sg.x() : sg.y())});
	target_ = target.translated(-sg.topLeft());
	if (!isVisible()) show();
	update();
}

void GuideOverlay::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QColor fill = accent, edge = accent;
	fill.setAlpha(40);
	edge.setAlpha(230);
	p.setBrush(fill);
	p.setPen(QPen(edge, 2, Qt::DashLine));
	p.drawRoundedRect(target_.adjusted(1, 1, -1, -1), 10, 10);
	p.setPen(QPen(edge, 1, Qt::DashLine));
	for (auto &[o, pos] : lines_) {
		if (o == 'v') p.drawLine(pos, 0, pos, height());
		else p.drawLine(0, pos, width(), pos);
	}
}

EditFrame::EditFrame(QWidget *dock, const QString &a) : QWidget(dock), accent(a)
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();
}

void EditFrame::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QColor c = accent;
	c.setAlpha(230);
	p.setPen(QPen(c, 2, Qt::DashLine));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
}

} // namespace ovl::ui
