#pragma once
#include "../core/service.hpp"
#include <QScrollArea>
#include <QVBoxLayout>
#include <functional>

namespace ovl::ui {

// Everything configurable, in sections. Changes apply immediately and are saved to the settings file.
class SettingsPage : public QWidget {
	Q_OBJECT
public:
	// on_change(restart_backend): refresh windows after a setting changed
	SettingsPage(Service &svc, std::function<void(bool)> on_change, QWidget *parent = nullptr);
signals:
	void closed();
private:
	QWidget *section(const QString &title);
	void toggle(QWidget *card, const char *key, const QString &title, const QString &desc, bool restart = false);
	void slider(QWidget *card, const char *key, int lo, int hi, const QString &title, const QString &desc, const QString &suffix = {}, bool restart = false);
	void choice(QWidget *card, const char *key, const QString &title, const QString &desc, const QList<QPair<QString, QString>> &items, bool restart = false);
	void text(QWidget *card, const char *key, const QString &title, const QString &desc, bool folder = false);
	void spin(QWidget *card, const char *key, double lo, double hi, double step, const QString &title, const QString &desc);
	QWidget *item(QWidget *card, const QString &title, const QString &desc);
	void changed(bool restart = false);
	Service &svc_;
	Settings &cfg_;
	std::function<void(bool)> on_change_;
	QVBoxLayout *col_;
};

} // namespace ovl::ui
