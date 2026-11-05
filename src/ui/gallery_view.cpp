#include "gallery_view.hpp"
#include "common.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <algorithm>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QPixmap>
#include <QScreen>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace ovl::ui {

static QIcon placeholder(const Media &m) { return icon(m.video ? "film" : "camera", FG_DIM); }
static QIcon thumb_icon(MediaStore &s, const Media &m)
{
	QImage t = s.thumb(m);
	return t.isNull() ? placeholder(m) : QIcon(QPixmap::fromImage(t));
}

void show_media_menu(QWidget *parent, MediaStore &store, const QString &path, const QPoint &pos, const std::function<void(const QString &)> &open_cb)
{
	QMenu m(parent);
	apply_style(&m);
	m.addAction(icon("play"), "Open", [=] { open_cb(path); });
	m.addAction(icon("folder"), "Show in folder", [=] { gallery::reveal(path); });
	m.addAction(icon("copy"), "Copy path", [=] { QApplication::clipboard()->setText(QDir::toNativeSeparators(path)); });
	m.addAction(icon("star"), store.is_fav(path) ? "Remove from favorites" : "Add to favorites", [&store, path] { store.toggle_fav(path); });
	m.addSeparator();
	m.addAction(icon("pencil"), "Rename...", [&store, path, parent] {
		QInputDialog dlg(nullptr);
		dlg.setWindowTitle("Rename");
		dlg.setLabelText("New name (without extension)");
		dlg.setTextValue(QFileInfo(path).completeBaseName());
		prepare_dialog(&dlg);
		if (dlg.exec() != QDialog::Accepted) return;
		try {
			gallery::rename_file(path, dlg.textValue().trimmed());
			store.forget(path);
			store.rescan();
		} catch (const QString &err) {
			message("Cannot rename", err);
		}
		(void)parent;
	});
	m.addAction(icon("trash"), "Delete (to recycle bin)", [&store, path] {
		if (!confirm("Delete", QString("Move \"%1\" to the recycle bin?").arg(QFileInfo(path).fileName()), true)) return;
		if (gallery::move_to_trash(path)) store.forget(path);
		else message("Cannot delete", "The file could not be moved to the recycle bin.");
	});
	m.exec(pos);
}

ImageViewer::ImageViewer(const QString &path, QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QFileInfo(path).fileName());
	auto *l = new QVBoxLayout(this);
	l->setContentsMargins(8, 8, 8, 8);
	auto *lab = new QLabel;
	lab->setAlignment(Qt::AlignCenter);
	QPixmap pm(path);
	const QRect g = target_screen()->availableGeometry();
	lab->setPixmap(pm.scaled(int(g.width() * 0.8), int(g.height() * 0.8), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	l->addWidget(lab);
	prepare_dialog(this);
}

// ---- full gallery -----------------------------------------------------------------------------------------------------------
GalleryWindow::GalleryWindow(MediaStore &store, QWidget *parent) : QWidget(parent), store_(store)
{
	setObjectName("plain");
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(12, 10, 12, 10);
	auto *top = new QHBoxLayout;
	search_ = new QLineEdit;
	search_->setPlaceholderText("Search captures");
	search_->setClearButtonEnabled(true);
	filter_ = new QComboBox;
	filter_->addItems({"All", "Recordings", "Clips", "Screenshots", "Favorites"});
	sort_ = new QComboBox;
	sort_->addItems({"Newest", "Oldest", "Name", "Largest"});
	size_ = new QSlider(Qt::Horizontal);
	size_->setRange(100, 320);
	size_->setValue(qBound(100, store_.settings().i("thumb_size"), 320));
	size_->setFixedWidth(90);
	size_->setToolTip("Picture size");
	size_->setAccessibleName("Picture size");
	top->addWidget(search_, 1);
	top->addWidget(filter_);
	top->addWidget(sort_);
	top->addWidget(new QLabel("Size"));
	top->addWidget(size_);
	list_ = new QListWidget;
	list_->setViewMode(QListView::IconMode);
	list_->setResizeMode(QListView::Adjust);
	list_->setMovement(QListView::Static);
	list_->setWordWrap(true);
	list_->setContextMenuPolicy(Qt::CustomContextMenu);
	auto *info = new QLabel("Videos open in your default player. Right-click a capture for more.");
	info->setObjectName("dim");
	v->addLayout(top);
	v->addWidget(list_, 1);
	v->addWidget(info);
	rebuild_timer_.setSingleShot(true);
	rebuild_timer_.setInterval(120);
	connect(&rebuild_timer_, &QTimer::timeout, this, &GalleryWindow::rebuild);
	auto *size_save = new QTimer(this); // save once the slider settles, not on every pixel
	size_save->setSingleShot(true);
	size_save->setInterval(300);
	connect(size_save, &QTimer::timeout, this, [this] { store_.settings().set("thumb_size", size_->value()); store_.settings().save(); });
	connect(size_, &QSlider::valueChanged, this, [this, size_save] { apply_size(); size_save->start(); });
	connect(search_, &QLineEdit::textChanged, &rebuild_timer_, qOverload<>(&QTimer::start));
	connect(filter_, &QComboBox::currentIndexChanged, &rebuild_timer_, qOverload<>(&QTimer::start));
	connect(sort_, &QComboBox::currentIndexChanged, &rebuild_timer_, qOverload<>(&QTimer::start));
	connect(&store_, &MediaStore::changed, &rebuild_timer_, qOverload<>(&QTimer::start));
	connect(&store_, &MediaStore::thumb_ready, this, [this](const QString &path) {
		for (int i = 0; i < list_->count(); ++i) {
			auto *it = list_->item(i);
			if (it->data(Qt::UserRole).toString() != path) continue;
			QImage t = store_.thumb(*std::find_if(store_.items().begin(), store_.items().end(), [&](const Media &m) { return m.path == path; }));
			if (!t.isNull()) it->setIcon(QIcon(QPixmap::fromImage(t)));
		}
	});
	connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) { emit open_media(it->data(Qt::UserRole).toString()); });
	connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p) {
		if (auto *it = list_->itemAt(p))
			show_media_menu(this, store_, it->data(Qt::UserRole).toString(), list_->viewport()->mapToGlobal(p), [this](const QString &path) { emit open_media(path); });
	});
}

void GalleryWindow::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	store_.rescan();
	rebuild();
}

std::vector<const Media *> GalleryWindow::filtered() const
{
	std::vector<const Media *> out;
	const QString q = search_->text().trimmed().toLower();
	const int f = filter_->currentIndex();
	for (auto &m : store_.items()) {
		if (!q.isEmpty() && !(m.name.toLower().contains(q) || m.game.toLower().contains(q))) continue;
		if (f == 1 && !(m.video && !m.is_clip)) continue;
		if (f == 2 && !m.is_clip) continue;
		if (f == 3 && m.video) continue;
		if (f == 4 && !store_.is_fav(m.path)) continue;
		out.push_back(&m);
	}
	switch (sort_->currentIndex()) {
	case 1: std::sort(out.begin(), out.end(), [](auto *a, auto *b) { return a->mtime < b->mtime; }); break;
	case 2: std::sort(out.begin(), out.end(), [](auto *a, auto *b) { return a->name.compare(b->name, Qt::CaseInsensitive) < 0; }); break;
	case 3: std::sort(out.begin(), out.end(), [](auto *a, auto *b) { return a->size > b->size; }); break;
	default: std::sort(out.begin(), out.end(), [](auto *a, auto *b) { return a->mtime > b->mtime; });
	}
	return out;
}

// Applies the picture size from the slider to the grid right away (thumbnails are scaled, never regenerated).
void GalleryWindow::apply_size()
{
	const int s = size_->value(), h = s * 9 / 16;
	list_->setIconSize(QSize(s, h));
	list_->setGridSize(QSize(s + 18, h + 44));
}

void GalleryWindow::rebuild()
{
	list_->clear();
	apply_size();
	int idx = 0;
	for (const Media *m : filtered()) {
		auto *it = new QListWidgetItem(thumb_icon(store_, *m), store_.display_name(*m, idx++));
		it->setData(Qt::UserRole, m->path);
		it->setToolTip(store_.display_name(*m, idx - 1) + "\n" + m->folder + "\n" + gallery::fmt_size((double)m->size) + "  " +
			       QDateTime::fromMSecsSinceEpoch(m->mtime).toString("dd MMM yyyy HH:mm"));
		list_->addItem(it);
	}
}

// ---- recent strip ---------------------------------------------------------------------------------------------------------------
GalleryStrip::GalleryStrip(MediaStore &store) : store_(store)
{
	setWidgetResizable(true);
	setFrameShape(QFrame::NoFrame);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	pending_.setSingleShot(true);
	pending_.setInterval(80);
	connect(&pending_, &QTimer::timeout, this, &GalleryStrip::build);
	connect(&store_, &MediaStore::changed, this, &GalleryStrip::rebuild);
	connect(&store_, &MediaStore::thumb_ready, this, &GalleryStrip::rebuild);
	build();
}

void GalleryStrip::build()
{
	const int pw = qBound(80, store_.settings().i("strip_size"), 240), ph = pw * 5 / 8; // 8:5 keeps the old look at 112 px
	setFixedHeight(ph + 16);
	auto *host = new QWidget;
	host->setObjectName("plain");
	auto *h = new QHBoxLayout(host);
	h->setContentsMargins(12, 4, 12, 4);
	h->setSpacing(6);
	int n = 0;
	for (auto &m : store_.items()) {
		if (n++ >= 12) break;
		auto *b = new QToolButton;
		b->setFixedSize(pw, ph);
		b->setIconSize(QSize(pw - 4, ph - 4));
		b->setIcon(thumb_icon(store_, m));
		b->setToolTip(store_.display_name(m, n - 1));
		b->setContextMenuPolicy(Qt::CustomContextMenu);
		const QString path = m.path;
		connect(b, &QToolButton::clicked, this, [this, path] { emit activated(path); });
		connect(b, &QToolButton::customContextMenuRequested, this, [this, b, path](const QPoint &p) {
			show_media_menu(this, store_, path, b->mapToGlobal(p), [this](const QString &q) { emit activated(q); });
		});
		h->addWidget(b);
	}
	if (store_.items().empty()) {
		auto *l = new QLabel("No captures found in the gallery folder yet");
		l->setObjectName("dim");
		h->addWidget(l);
	}
	h->addStretch(1);
	setWidget(host);
}

} // namespace ovl::ui
