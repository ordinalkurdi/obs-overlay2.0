#pragma once
#include "../core/gallery.hpp"
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QTimer>

namespace ovl::ui {

void show_media_menu(QWidget *parent, MediaStore &store, const QString &path, const QPoint &global_pos, const std::function<void(const QString &)> &open_cb);

// Screenshot viewer (videos open in the system's default player: OBS ships no media player widget for plugins).
class ImageViewer : public QDialog {
	Q_OBJECT
public:
	ImageViewer(const QString &path, QWidget *parent = nullptr);
};

// Full gallery page: search, filter, sort, lazy thumbnails.
class GalleryWindow : public QWidget {
	Q_OBJECT
public:
	explicit GalleryWindow(MediaStore &store, QWidget *parent = nullptr);
signals:
	void open_media(const QString &path);
protected:
	void showEvent(QShowEvent *) override;
private:
	void rebuild();
	void apply_size();
	std::vector<const Media *> filtered() const;
	MediaStore &store_;
	QLineEdit *search_;
	QComboBox *filter_, *sort_;
	QSlider *size_;
	QListWidget *list_;
	QTimer rebuild_timer_;
};

// "Recent captures" strip on the home page.
class GalleryStrip : public QScrollArea {
	Q_OBJECT
public:
	explicit GalleryStrip(MediaStore &store);
	void rebuild() { pending_.start(); }
signals:
	void activated(const QString &path);
private:
	void build();
	MediaStore &store_;
	QTimer pending_;
};

} // namespace ovl::ui
