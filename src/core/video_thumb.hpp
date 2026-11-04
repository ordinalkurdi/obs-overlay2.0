#pragma once
#include <QImage>
#include <QString>

namespace ovl {
// Decodes one frame from a video using the FFmpeg DLLs that ship with OBS, loaded at RUNTIME (never linked): if they
// are missing or are a version this code does not know, the function simply returns a null image and the plugin keeps
// working. Handles whatever OBS can record (mkv, mp4, mov, flv ...).
QImage ffmpeg_thumbnail(const QString &path, int size);
}
