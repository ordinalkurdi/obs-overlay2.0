// Media gallery data: recursive scan (per-game and replay sub-folders), cached thumbnails, safe file operations.
#pragma once
#include "settings.hpp"
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <vector>

namespace ovl {

struct Media {
	QString path, name, game, folder;
	bool video = false;  // else image
	bool is_clip = false; // lives in a replay / clip folder or has "replay" / "clip" in its path
	qint64 size = 0;
	qint64 mtime = 0; // ms since epoch
};

namespace gallery {
std::vector<Media> scan(const QString &root, int max_depth = 8);
QImage make_thumbnail(const QString &path, int size);       // null when no picture can be made
QString fmt_size(double bytes);
bool valid_filename(const QString &name);
QString rename_file(const QString &path, const QString &new_stem); // returns the new path, throws QString on error
bool move_to_trash(const QString &path);
void reveal(const QString &path);
void open_external(const QString &path);
} // namespace gallery

// Owns the scanned list, thumbnails and favorites. Scans and thumbnailing run on the thread pool.
class MediaStore : public QObject {
	Q_OBJECT
public:
	MediaStore(Settings &cfg, const QString &cache_dir, QObject *parent = nullptr);
	Settings &settings() { return cfg_; }
	const std::vector<Media> &items() const { return items_; }
	void rescan();
	QImage thumb(const Media &m); // returns a cached thumbnail or a null image and requests one
	void forget(const QString &path);
	bool is_fav(const QString &path) const;
	void toggle_fav(const QString &path);
	QString display_name(const Media &m, int idx) const;
	void clear_history(); // thumbnails + favorites

signals:
	void changed();
	void thumb_ready(const QString &path);

private:
	Settings &cfg_;
	QString cache_;
	std::vector<Media> items_;
	QHash<QString, QImage> thumbs_;
	QSet<QString> pending_, failed_;
	bool scanning_ = false, again_ = false;
};

} // namespace ovl
