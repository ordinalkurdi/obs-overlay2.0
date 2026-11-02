// Look of the overlay. The stylesheet is applied to OUR windows only, never application-wide: OBS must keep its theme.
#pragma once
#include "../core/settings.hpp"
#include <QColor>
#include <QIcon>
#include <QString>
#include <QWidget>

namespace ovl::ui {

inline const QString FG = "#f2f2f2", FG_DIM = "#9a9a9a", BG = "#181818", HEADER = "#303030", LINE = "#2c2c2c";

QString stylesheet(const Settings &cfg);
void set_settings(Settings *cfg);          // call once
void apply_style(QWidget *top_level);            // apply the stylesheet now and whenever settings change
void restyle_all();                        // after a settings change

// Lucide-style stroke icons, drawn from inline SVG (no icon font, no extra dependency).
QIcon icon(const QString &name, const QString &color = FG);
QPixmap icon_pixmap(const QString &name, int size, const QString &color = FG);

} // namespace ovl::ui
