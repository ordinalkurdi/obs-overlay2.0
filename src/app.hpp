// Everything the plugin owns, created once OBS has finished loading and destroyed when OBS unloads the module.
#pragma once
#include "core/gallery.hpp"
#include "core/hotkeys.hpp"
#include "core/service.hpp"
#include "core/settings.hpp"
#include "ui/layout_edit.hpp"
#include "ui/overlay.hpp"
#include "ui/widgets_page.hpp"
#include <QMenu>
#include <QSystemTrayIcon>
#include <memory>

namespace ovl {

class App : public QObject {
	Q_OBJECT
public:
	App();
	~App() override;
	void save_hotkeys(void *obs_data) { hotkeys_.save(obs_data); }
	void load_hotkeys(void *obs_data) { hotkeys_.load(obs_data); }
	void shutdown_ui();

private:
	void on_action(const QString &action);
	void open_settings();
	void open_media(const QString &path);
	void build_tray();
	void apply_changes(bool restart_backend);
	void toggle_widgets();

	std::unique_ptr<Settings> cfg_;
	std::unique_ptr<Service> svc_;
	std::unique_ptr<MediaStore> store_;
	Hotkeys hotkeys_;
	ui::Overlay *overlay_ = nullptr;
	std::unique_ptr<ui::WidgetManager> widgets_;
	std::unique_ptr<ui::PresetController> presets_;
	std::unique_ptr<ui::LayoutEditor> editor_;
	ui::GalleryWindow *gallery_ = nullptr;
	ui::WidgetsPage *widgets_page_ = nullptr;
	QSystemTrayIcon *tray_ = nullptr;
	QMenu *tray_menu_ = nullptr;
};

} // namespace ovl
