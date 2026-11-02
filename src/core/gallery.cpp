#include "gallery.hpp"
#include "video_thumb.hpp"
#include <QCryptographicHash>
#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#endif

namespace ovl {

namespace {
const QSet<QString> VIDEO_EXT = {"mp4", "mkv", "mov", "webm", "flv", "avi", "ts"};
const QSet<QString> IMAGE_EXT = {"png", "jpg", "jpeg"};

#ifdef _WIN32
// Windows already knows how to draw a video's thumbnail (same as Explorer); no codecs needed in the plugin.
QImage shell_thumb(const QString &path, int size)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	QImage out;
	IShellItemImageFactory *f = nullptr;
	if (SUCCEEDED(SHCreateItemFromParsingName((PCWSTR)path.utf16(), nullptr, IID_PPV_ARGS(&f))) && f) {
		HBITMAP hb = nullptr;
		SIZE sz = {size, size};
		if (SUCCEEDED(f->GetImage(sz, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hb)) && hb) {
			BITMAP bm = {};
			GetObject(hb, sizeof bm, &bm);
			BITMAPINFO bi = {};
			bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth = bm.bmWidth;
			bi.bmiHeader.biHeight = -bm.bmHeight;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bi.bmiHeader.biCompression = BI_RGB;
			out = QImage(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
			HDC dc = GetDC(nullptr);
			GetDIBits(dc, hb, 0, (UINT)bm.bmHeight, out.bits(), &bi, DIB_RGB_COLORS);
			ReleaseDC(nullptr, dc);
			DeleteObject(hb);
			out = out.convertToFormat(QImage::Format_RGB32);
		}
		f->Release();
	}
	return out;
}
#endif
} // namespace

namespace gallery {

std::vector<Media> scan(const QString &root, int max_depth)
{
	std::vector<Media> out;
	QDir rootd(root);
	if (!rootd.exists()) return out;
	QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString path = it.next();
		QFileInfo fi(path);
		const QString rel = rootd.relativeFilePath(fi.absolutePath());
		const QString relDir = rel == "." ? QString() : rel;
		const QStringList parts = relDir.isEmpty() ? QStringList() : relDir.split('/');
		if (parts.size() >= max_depth) continue;
		bool hidden = false;
		for (auto &p : parts) if (p.startsWith('.') || p.compare("$recycle.bin", Qt::CaseInsensitive) == 0) hidden = true;
		if (hidden) continue;
		const QString ext = fi.suffix().toLower();
		const bool vid = VIDEO_EXT.contains(ext), img = IMAGE_EXT.contains(ext);
		if (!vid && !img) continue;
		Media m;
		m.path = fi.absoluteFilePath();
		m.name = fi.fileName();
		m.video = vid;
		m.game = parts.isEmpty() ? QStringLiteral("(root)") : parts.first();
		m.folder = relDir;
		const QString low = (relDir + "/" + m.name).toLower();
		m.is_clip = vid && (low.contains("replay") || low.contains("clip"));
		m.size = fi.size();
		m.mtime = fi.lastModified().toMSecsSinceEpoch();
		out.push_back(std::move(m));
	}
	return out;
}

QImage make_thumbnail(const QString &path, int size)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	QImage img;
	if (IMAGE_EXT.contains(ext)) {
		img = QImage(path);
	} else if (VIDEO_EXT.contains(ext)) {
		img = ffmpeg_thumbnail(path, size); // FFmpeg from OBS: handles mkv/mp4/mov/flv, whatever OBS records
#ifdef _WIN32
		if (img.isNull()) img = shell_thumb(path, size); // fallback: Explorer's own thumbnail
#endif
	}
	if (img.isNull()) return {};
	return img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString fmt_size(double n)
{
	static const char *u[] = {"B", "KB", "MB", "GB"};
	for (int i = 0; i < 4; ++i) {
		if (n < 1024 || i == 3) return i == 0 ? QString("%1 B").arg(n, 0, 'f', 0) : QString("%1 %2").arg(n, 0, 'f', 1).arg(u[i]);
		n /= 1024;
	}
	return {};
}

bool valid_filename(const QString &name)
{
	if (name.isEmpty() || name == "." || name == ".." || name.size() >= 200) return false;
	for (QChar c : QStringLiteral("\\/:*?\"<>|")) if (name.contains(c)) return false;
	return !name.contains(QChar(0));
}

QString rename_file(const QString &path, const QString &new_stem)
{
	QFileInfo fi(path);
	const QString name = new_stem + (fi.suffix().isEmpty() ? QString() : "." + fi.suffix());
	if (!valid_filename(name)) throw QString("Invalid file name");
	const QString dest = fi.dir().filePath(name);
	if (QFileInfo::exists(dest)) throw QString("%1 already exists").arg(name);
	if (!QFile::rename(path, dest)) throw QString("Could not rename the file");
	return dest;
}

bool move_to_trash(const QString &path) { return QFile::moveToTrash(path); } // recoverable

void reveal(const QString &path)
{
	const QString p = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
#if defined(_WIN32)
	QProcess::startDetached("explorer", {"/select,", p});
#elif defined(__APPLE__)
	QProcess::startDetached("open", {"-R", p});
#else
	QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void open_external(const QString &path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }

} // namespace gallery

// ---- MediaStore ---------------------------------------------------------------------------------------------
namespace {
template<class F> class Job : public QRunnable {
public:
	explicit Job(F f) : f_(std::move(f)) {}
	void run() override { f_(); }
private:
	F f_;
};
template<class F> void run_async(F f) { QThreadPool::globalInstance()->start(new Job<F>(std::move(f))); }
} // namespace

MediaStore::MediaStore(Settings &cfg, const QString &cache_dir, QObject *parent) : QObject(parent), cfg_(cfg), cache_(cache_dir) {}

void MediaStore::rescan()
{
	if (scanning_) { again_ = true; return; }
	scanning_ = true;
	QPointer<MediaStore> self(this);
	const QString root = cfg_.s("gallery_root");
	run_async([self, root] {
		auto res = gallery::scan(root);
		if (!self) return;
		QMetaObject::invokeMethod(self.data(), [self, res = std::move(res)]() mutable {
			if (!self) return;
			std::sort(res.begin(), res.end(), [](const Media &a, const Media &b) { return a.mtime > b.mtime; });
			self->items_ = std::move(res);
			self->scanning_ = false;
			emit self->changed();
			if (self->again_) { self->again_ = false; self->rescan(); }
		}, Qt::QueuedConnection);
	});
}

QImage MediaStore::thumb(const Media &m)
{
	auto it = thumbs_.find(m.path);
	if (it != thumbs_.end()) return it.value();
	if (failed_.contains(m.path) || pending_.contains(m.path)) return {};
	pending_.insert(m.path);
	QPointer<MediaStore> self(this);
	const int size = qMax(160, cfg_.i("thumb_size") * 2);
	const QString path = m.path, cache = cache_;
	const qint64 mt = m.mtime, sz = m.size;
	run_async([self, path, cache, size, mt, sz] {
		const QString key = QCryptographicHash::hash(QString("%1|%2|%3|%4").arg(path).arg(mt).arg(sz).arg(size).toUtf8(), QCryptographicHash::Sha1).toHex();
		const QString file = cache + "/" + key + ".png";
		QImage img;
		if (QFileInfo::exists(file)) img = QImage(file);
		if (img.isNull()) {
			img = gallery::make_thumbnail(path, size);
			if (!img.isNull()) { QDir().mkpath(cache); img.save(file, "PNG"); }
		}
		if (!self) return;
		QMetaObject::invokeMethod(self.data(), [self, path, img] {
			if (!self) return;
			self->pending_.remove(path);
			if (img.isNull()) { self->failed_.insert(path); return; }
			self->thumbs_.insert(path, img);
			emit self->thumb_ready(path);
		}, Qt::QueuedConnection);
	});
	return {};
}

void MediaStore::forget(const QString &path)
{
	items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Media &m) { return m.path == path; }), items_.end());
	thumbs_.remove(path);
	QStringList f = cfg_.list("favorites");
	if (f.removeAll(path)) { cfg_.set_list("favorites", f); cfg_.save(); }
	emit changed();
}

bool MediaStore::is_fav(const QString &path) const { return cfg_.list("favorites").contains(path); }

void MediaStore::toggle_fav(const QString &path)
{
	QStringList f = cfg_.list("favorites");
	if (!f.removeAll(path)) f << path;
	cfg_.set_list("favorites", f);
	cfg_.save();
	emit changed();
}

QString MediaStore::display_name(const Media &m, int idx) const
{
	return cfg_.b("hide_filenames") ? QString("%1 %2").arg(m.video ? "Video" : "Image").arg(idx + 1) : m.name;
}

void MediaStore::clear_history()
{
	thumbs_.clear(); failed_.clear();
	QDir(cache_).removeRecursively();
	cfg_.set_list("favorites", {});
	cfg_.save();
	emit changed();
}

} // namespace ovl
