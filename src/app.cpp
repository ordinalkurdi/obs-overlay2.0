#include "app.hpp"
#include "ui/gallery_view.hpp"
#include "ui/settings_page.hpp"
#include "ui/style.hpp"
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <obs-frontend-api.h>
#include <obs-module.h>

namespace ovl {

static QString config_file(const char *name)
{
	char *p = obs_module_config_path(name);
	QString s = p ? QString::fromUtf8(p) : QDir::home().filePath(QString(".obs-overlay-") + name);
	bfree(p);
	return s;
}

App::App()
{
	cfg_ = std::make_unique<Settings>(config_file("settings.json"));
	ui::set_settings(cfg_.get());
	svc_ = std::make_unique<Service>(*cfg_);
	store_ = std::make_unique<MediaStore>(*cfg_, QFileInfo(config_file("settings.json")).absolutePath() + "/thumbs");

	overlay_ = new ui::Overlay(*svc_, *store_);
	ui::set_screen_provider([this] { return overlay_->screen(); }); // dialogs open on the overlay's monitor
	widgets_ = std::make_unique<ui::WidgetManager>(*svc_);
	presets_ = std::make_unique<ui::PresetController>(*widgets_, *svc_);
	editor_ = std::make_unique<ui::LayoutEditor>(*widgets_, *presets_);

	gallery_ = new ui::GalleryWindow(*store_);
	widgets_page_ = new ui::WidgetsPage(*widgets_, *presets_, *editor_);
	overlay_->set_gallery(gallery_);
	overlay_->set_widgets_page(widgets_page_);

	connect(overlay_->gal_row, &ui::Row::clicked, this, [this] { overlay_->show_page("gallery"); });
	connect(overlay_->stat_row, &ui::Row::clicked, this, [this] { overlay_->show_page("widgets"); });
	connect(overlay_->strip, &ui::GalleryStrip::activated, this, &App::open_media);
	connect(gallery_, &ui::GalleryWindow::open_media, this, &App::open_media);
	connect(overlay_, &ui::Overlay::open_settings, this, &App::open_settings);
	connect(svc_.get(), &Service::media_created, store_.get(), &MediaStore::rescan);

	hotkeys_.register_all();
	connect(&hotkeys_, &Hotkeys::triggered, this, &App::on_action);

	svc_->start();
	widgets_->apply(false); // restore floating widgets that were visible last time
	build_tray();

	// Tools menu entry in OBS itself
	if (auto *act = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("OBS Overlay"))) connect(act, &QAction::triggered, overlay_, &ui::Overlay::toggle_visible);
	if (!cfg_->b("start_hidden")) overlay_->show_panel();
}

App::~App()
{
	shutdown_ui();
	hotkeys_.unregister_all();
	delete tray_;
	delete tray_menu_;
	editor_.reset();
	presets_.reset();
	widgets_.reset();
	delete overlay_; // owns the pages
	svc_.reset();
}

void App::shutdown_ui()
{
	if (editor_) editor_->stop();
	if (overlay_) overlay_->hide();
	if (widgets_) for (auto *d : widgets_->docks) d->hide();
	if (tray_) tray_->hide();
	if (svc_) svc_->shutdown();
}

void App::open_media(const QString &path)
{
	if (path.isEmpty() || !QFileInfo::exists(path)) { svc_->say("warn", "That file no longer exists"); store_->rescan(); return; }
	const QString ext = QFileInfo(path).suffix().toLower();
	if (ext == "png" || ext == "jpg" || ext == "jpeg") { ui::ImageViewer v(path); v.exec(); }
	else gallery::open_external(path); // videos open in the system's default player
}

void App::open_settings()
{
	auto *page = new ui::SettingsPage(*svc_, [this](bool r) { apply_changes(r); });
	connect(page, &ui::SettingsPage::closed, overlay_, [this] { overlay_->drop_settings(); });
	overlay_->set_settings_page(page);
}

void App::apply_changes(bool restart)
{
	ui::restyle_all();
	overlay_->apply_settings();
	overlay_->strip->rebuild(); // picture size of the recent-captures strip
	if (restart) svc_->apply_settings();
	widgets_->apply(false);
	store_->rescan();
	overlay_->sync();
}

void App::toggle_widgets()
{
	cfg_->set("stats_visible", !cfg_->b("stats_visible"));
	cfg_->save();
	widgets_->apply();
	svc_->refresh();
	overlay_->sync();
}

void App::on_action(const QString &a)
{
	if (a == "toggle_overlay") overlay_->toggle_visible();
	else if (a == "stats") toggle_widgets();
	else if (a == "widgets_click") widgets_->toggle_click_through_all();
	else if (a == "layout_edit") editor_->toggle();
}

void App::build_tray()
{
	if (!cfg_->b("show_tray") || !QSystemTrayIcon::isSystemTrayAvailable()) return;
	tray_menu_ = new QMenu;
	ui::apply_style(tray_menu_);
	tray_menu_->addAction("Show / hide overlay", overlay_, &ui::Overlay::toggle_visible);
	tray_menu_->addAction("Start / stop recording", overlay_, &ui::Overlay::rec_clicked);
	tray_menu_->addAction("Save replay", svc_.get(), &Service::save_replay);
	tray_menu_->addAction("Screenshot", svc_.get(), &Service::screenshot);
	tray_menu_->addSeparator();
	tray_menu_->addAction("Show / hide floating widgets", this, &App::toggle_widgets);
	tray_menu_->addAction("Edit widget layout", editor_.get(), &ui::LayoutEditor::toggle);
	tray_menu_->addAction("Toggle widget click-through", widgets_.get(), &ui::WidgetManager::toggle_click_through_all);
	tray_menu_->addSeparator();
	tray_menu_->addAction("Settings", this, [this] { overlay_->show_panel(); open_settings(); });
	tray_ = new QSystemTrayIcon(ui::icon("record", cfg_->s("accent")), nullptr);
	tray_->setToolTip("OBS Overlay");
	tray_->setContextMenu(tray_menu_);
	connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) { if (r == QSystemTrayIcon::Trigger) overlay_->toggle_visible(); });
	tray_->show();
}

} // namespace ovl
