#include "style.hpp"
#include <QByteArray>
#include <algorithm>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QSvgRenderer>
#include <vector>

namespace ovl::ui {

static Settings *g_cfg = nullptr;
static std::vector<QPointer<QWidget>> g_styled;
static QString g_css;

void set_settings(Settings *cfg) { g_cfg = cfg; g_css = stylesheet(*cfg); }

QString stylesheet(const Settings &cfg)
{
	const QString a = cfg.s("accent");
	const int fs = cfg.i("font_size"), r = cfg.i("radius"), pad = cfg.i("row_pad");
	const QString css = R"(
	* { font-family: 'Segoe UI', 'Inter', 'Helvetica Neue', sans-serif; font-size: %FS%px; color: %FG%; }
	#panel { background: %BG%; border-radius: %R%px; border: 1px solid #000; }
	#header { background: %HEADER%; border-top-left-radius: %R%px; border-top-right-radius: %R%px; }
	#title { font-weight: 600; font-size: %FS3%px; letter-spacing: 1px; }
	#sep { background: %LINE%; max-height: 1px; min-height: 1px; }
	#dim { color: %DIM%; font-size: %FS2%px; }
	#row { background: transparent; border-radius: 6px; padding: %PAD%px 0px; }
	#row:hover { background: #232323; }
	#row:focus { background: #262626; border: 1px solid %A%; }
	QToolButton { background: transparent; border: none; border-radius: 6px; padding: 4px; }
	QToolButton:hover { background: #3a3a3a; }
	QToolButton:focus { border: 1px solid %A%; }
	QToolButton:checked { background: #3a3a3a; }
	QPushButton { background: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 6px; padding: 6px 12px; }
	QPushButton:hover { background: #343434; }
	QPushButton:focus { border: 1px solid %A%; }
	QPushButton#primary { background: %A%; color: #07140d; border: none; font-weight: 600; }
	QPushButton#danger { background: #7a2323; border: none; }
	QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #222; border: 1px solid #3a3a3a; border-radius: 5px; padding: 4px 6px; }
	QComboBox QAbstractItemView { background: #222; selection-background-color: #3a3a3a; }
	QSlider::groove:horizontal { height: 4px; background: #3a3a3a; border-radius: 2px; }
	QSlider::sub-page:horizontal { background: %A%; border-radius: 2px; }
	QSlider::handle:horizontal { background: #fff; width: 12px; margin: -5px 0; border-radius: 6px; }
	QListWidget { background: %BG%; border: none; }
	QListWidget::item:selected { background: #2d2d2d; border-radius: 6px; }
	QScrollArea { background: transparent; border: none; }
	QScrollBar:horizontal { height: 6px; background: transparent; }
	QScrollBar::handle:horizontal { background: #444; border-radius: 3px; min-width: 20px; }
	QScrollBar:vertical { width: 6px; background: transparent; }
	QScrollBar::handle:vertical { background: #444; border-radius: 3px; min-height: 20px; }
	QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
	QToolTip { background: #111; color: %FG%; border: 1px solid #444; padding: 3px; }
	QMenu { background: #222; border: 1px solid #3a3a3a; padding: 4px; }
	QMenu::item { padding: 5px 18px; border-radius: 4px; }
	QMenu::item:selected { background: #3a3a3a; }
	QMenu::separator { height: 1px; background: #3a3a3a; margin: 4px 6px; }
	#card { background: #1e1e1e; border: 1px solid %LINE%; border-radius: %R%px; }
	#sectionTitle { color: %DIM%; font-size: %FS2%px; font-weight: 600; letter-spacing: 1px; }
	#pageTitle { font-size: %FS5%px; font-weight: 600; }
	#settingTitle { font-weight: 600; }
	#valueLabel { color: %A%; font-weight: 600; }
	#setting { background: transparent; }
	QPushButton#link { background: transparent; border: none; color: %A%; padding: 4px 8px; font-weight: 600; }
	QPushButton#link:hover { background: #2a2a2a; }
	QPlainTextEdit { background: #1a1a1a; border: 1px solid #333; border-radius: 5px; font-family: Consolas, 'Cascadia Mono', monospace; }
	QDialog, QMessageBox, #plain { background: %BG%; }
	QCheckBox::indicator { width: 16px; height: 16px; }
	)";
	return QString(css).replace("%FS5%", QString::number(fs + 5)).replace("%FS3%", QString::number(fs + 3)).replace("%FS2%", QString::number(fs - 2))
		.replace("%FS%", QString::number(fs)).replace("%R%", QString::number(r)).replace("%PAD%", QString::number(pad))
		.replace("%A%", a).replace("%FG%", FG).replace("%BG%", BG).replace("%HEADER%", HEADER).replace("%LINE%", LINE).replace("%DIM%", FG_DIM);
}

void apply_style(QWidget *w)
{
	g_styled.erase(std::remove_if(g_styled.begin(), g_styled.end(), [](const QPointer<QWidget> &p) { return p.isNull(); }), g_styled.end());
	g_styled.push_back(w);
	w->setStyleSheet(g_css);
}

void restyle_all()
{
	if (!g_cfg) return;
	const QString css = stylesheet(*g_cfg);
	if (css == g_css) return; // re-styling every widget is costly: only when something changed
	g_css = css;
	for (auto &p : g_styled) if (p) p->setStyleSheet(css);
}

// ---- icons ---------------------------------------------------------------------------------------------------
static const QHash<QString, QString> &shapes()
{
	static const QHash<QString, QString> s = {
		{"record", "<circle cx='12' cy='12' r='9'/><circle cx='12' cy='12' r='3.5' fill='@C'/>"},
		{"stop-circle", "<circle cx='12' cy='12' r='9'/><rect x='9' y='9' width='6' height='6' rx='1'/>"},
		{"play", "<polygon points='7,4 20,12 7,20'/>"},
		{"stop", "<rect x='6' y='6' width='12' height='12' rx='2'/>"},
		{"pause-circle", "<circle cx='12' cy='12' r='9'/><line x1='10' y1='9' x2='10' y2='15'/><line x1='14' y1='9' x2='14' y2='15'/>"},
		{"replay", "<path d='M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8'/><path d='M3 3v5h5'/>"},
		{"save", "<path d='M5 3h11l3 3v13a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z'/><rect x='8' y='3' width='7' height='5'/><rect x='7' y='13' width='10' height='8'/>"},
		{"camera", "<path d='M4 8h3l2-3h6l2 3h3a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V9a1 1 0 0 1 1-1z'/><circle cx='12' cy='13.5' r='3.5'/>"},
		{"broadcast", "<circle cx='12' cy='12' r='2'/><path d='M16.2 7.8a6 6 0 0 1 0 8.4M7.8 16.2a6 6 0 0 1 0-8.4M19 5a10 10 0 0 1 0 14M5 19A10 10 0 0 1 5 5'/>"},
		{"webcam", "<circle cx='12' cy='10' r='7'/><circle cx='12' cy='10' r='2.5'/><path d='M7 21h10M12 17v4'/>"},
		{"scene", "<path d='M3 9h18v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z'/><path d='M3 9l2-5h3l-2 5M9 9l2-5h3l-2 5M15 9l2-5h3l-1 5'/>"},
		{"monitor", "<rect x='3' y='4' width='18' height='12' rx='2'/><path d='M8 21h8M12 16v5'/><circle cx='12' cy='10' r='2'/>"},
		{"layers", "<polygon points='12,3 21,8 12,13 3,8'/><polyline points='3,12 12,17 21,12'/><polyline points='3,16 12,21 21,16'/>"},
		{"volume", "<polygon points='3,9 7,9 12,5 12,19 7,15 3,15'/><path d='M16 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11'/>"},
		{"volume-off", "<polygon points='3,9 7,9 12,5 12,19 7,15 3,15'/><line x1='16' y1='9' x2='22' y2='15'/><line x1='22' y1='9' x2='16' y2='15'/>"},
		{"sliders", "<path d='M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6'/>"},
		{"dashboard", "<rect x='3' y='3' width='7' height='9' rx='1'/><rect x='14' y='3' width='7' height='5' rx='1'/><rect x='14' y='12' width='7' height='9' rx='1'/><rect x='3' y='16' width='7' height='5' rx='1'/>"},
		{"eye", "<path d='M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z'/><circle cx='12' cy='12' r='3'/>"},
		{"eye-off", "<path d='M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z'/><circle cx='12' cy='12' r='3'/><line x1='3' y1='3' x2='21' y2='21'/>"},
		{"lock", "<rect x='4' y='11' width='16' height='10' rx='2'/><path d='M8 11V7a4 4 0 0 1 8 0v4'/>"},
		{"unlock", "<rect x='4' y='11' width='16' height='10' rx='2'/><path d='M8 11V7a4 4 0 0 1 7.5-2'/>"},
		{"pin", "<path d='M12 17v5'/><path d='M9 3h6l-1 7 3 3v2H7v-2l3-3z'/>"},
		{"settings", "<path d='M20 7h-9'/><path d='M14 17H5'/><circle cx='17' cy='17' r='3'/><circle cx='7' cy='7' r='3'/>"},
		{"minimize", "<line x1='5' y1='12' x2='19' y2='12'/>"},
		{"close", "<line x1='6' y1='6' x2='18' y2='18'/><line x1='18' y1='6' x2='6' y2='18'/>"},
		{"arrow-left", "<line x1='19' y1='12' x2='5' y2='12'/><polyline points='12,19 5,12 12,5'/>"},
		{"chevron-right", "<polyline points='9,6 15,12 9,18'/>"},
		{"chevron-down", "<polyline points='6,9 12,15 18,9'/>"},
		{"film", "<rect x='3' y='3' width='18' height='18' rx='2'/><path d='M7 3v18M17 3v18M3 8h4M3 12h4M3 16h4M17 8h4M17 12h4M17 16h4'/>"},
		{"folder", "<path d='M3 6a2 2 0 0 1 2-2h4l2 3h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>"},
		{"search", "<circle cx='11' cy='11' r='7'/><line x1='21' y1='21' x2='16.5' y2='16.5'/>"},
		{"star", "<polygon points='12,2 15.1,8.6 22,9.3 16.8,14 18.2,21 12,17.5 5.8,21 7.2,14 2,9.3 8.9,8.6'/>"},
		{"trash", "<path d='M3 6h18M8 6V4h8v2M6 6l1 15h10l1-15'/>"},
		{"pencil", "<path d='M4 20l1-4L16 5l3 3L8 19z'/>"},
		{"copy", "<rect x='9' y='9' width='11' height='11' rx='2'/><path d='M5 15V6a2 2 0 0 1 2-2h9'/>"},
		{"external", "<path d='M14 4h6v6M20 4l-9 9M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5'/>"},
		{"move", "<path d='M12 3v18M3 12h18M9 6l3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3'/>"},
		{"pointer-off", "<path d='M5 3l14 8-6 2-2 6z'/><line x1='3' y1='3' x2='21' y2='21'/>"},
		{"plus", "<line x1='12' y1='5' x2='12' y2='19'/><line x1='5' y1='12' x2='19' y2='12'/>"},
		{"globe", "<circle cx='12' cy='12' r='9'/><ellipse cx='12' cy='12' rx='4' ry='9'/><line x1='3' y1='12' x2='21' y2='12'/>"},
		{"check", "<polyline points='4,12 10,18 20,6'/>"},
		{"restore", "<path d='M3 12a9 9 0 1 0 3-6.7L3 8'/><path d='M3 3v5h5'/>"},
		{"bookmark", "<path d='M6 3h12v18l-6-4-6 4z'/>"},
		{"dots", "<circle cx='12' cy='5' r='1.2' fill='@C'/><circle cx='12' cy='12' r='1.2' fill='@C'/><circle cx='12' cy='19' r='1.2' fill='@C'/>"},
		{"radio-on", "<circle cx='12' cy='12' r='9'/><circle cx='12' cy='12' r='4' fill='@C'/>"},
		{"radio-off", "<circle cx='12' cy='12' r='9'/>"},
	};
	return s;
}

QPixmap icon_pixmap(const QString &name, int size, const QString &color)
{
	QString body = shapes().value(name);
	body.replace("@C", color);
	const QByteArray svg = QString("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%1' stroke-width='2' "
				       "stroke-linecap='round' stroke-linejoin='round'>%2</svg>").arg(color, body).toUtf8();
	QPixmap pm(size * 2, size * 2); // drawn at 2x so it stays sharp on high-DPI screens
	pm.fill(Qt::transparent);
	QSvgRenderer r(svg);
	QPainter p(&pm);
	r.render(&p);
	p.end();
	pm.setDevicePixelRatio(2.0);
	return pm;
}

QIcon icon(const QString &name, const QString &color)
{
	QIcon ic;
	ic.addPixmap(icon_pixmap(name, 24, color));
	return ic;
}

} // namespace ovl::ui
