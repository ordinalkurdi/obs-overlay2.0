/*
OBS Overlay - an in-game control panel for OBS Studio (record, replay, screenshots, scenes, audio, widgets, gallery).
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later
version. (libobs is GPL-2.0-or-later, so plugins that link it must be too.)
*/
#include "app.hpp"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QApplication>
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-overlay", "en-US")

static std::unique_ptr<ovl::App> g_app;

static void on_save(obs_data_t *save_data, bool saving, void *)
{
	if (!g_app) return;
	saving ? g_app->save_hotkeys(save_data) : g_app->load_hotkeys(save_data);
}

static void on_frontend_event(enum obs_frontend_event e, void *)
{
	if (e == OBS_FRONTEND_EVENT_EXIT && g_app) g_app->shutdown_ui(); // close our windows before OBS tears the UI down
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-overlay] loaded (version %s)", OVL_VERSION);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	return true;
}

// Runs after every module is loaded and before the scene collection loads, so the hotkeys exist in time for OBS to
// restore their bindings. (Creating the UI here is fine: the main window already exists.)
void obs_module_post_load(void)
{
	if (g_app || !qApp) return;
	g_app = std::make_unique<ovl::App>();
	obs_frontend_add_save_callback(on_save, nullptr);
	blog(LOG_INFO, "[obs-overlay] ready (version %s)", OVL_VERSION);
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_frontend_remove_save_callback(on_save, nullptr);
	g_app.reset();
	blog(LOG_INFO, "[obs-overlay] unloaded");
}

const char *obs_module_description(void) { return "In-game overlay to control OBS: record, replay buffer, screenshots, scenes, audio, widgets and gallery"; }
const char *obs_module_name(void) { return "OBS Overlay"; }
