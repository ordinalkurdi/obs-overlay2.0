#pragma once
#include <QColor>
#include <QRect>
#include <QWidget>
#include <vector>

namespace ovl::ui {

// Click-through layer drawn on the screen while a widget is dragged: snap guide lines and the drop target.
class GuideOverlay : public QWidget {
public:
	explicit GuideOverlay(const QString &accent);
	void show_guides(const QRect &screen_geo, const std::vector<std::pair<char, int>> &guides, const QRect &target);
	QColor accent;
protected:
	void paintEvent(QPaintEvent *) override;
private:
	std::vector<std::pair<char, int>> lines_;
	QRect target_;
};

// Dashed outline drawn over a widget while editing (never intercepts the mouse).
class EditFrame : public QWidget {
public:
	EditFrame(QWidget *dock, const QString &accent);
	QColor accent;
protected:
	void paintEvent(QPaintEvent *) override;
};

} // namespace ovl::ui
