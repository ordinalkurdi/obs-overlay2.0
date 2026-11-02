#pragma once
#include <QImage>
#include <QString>

namespace ovl {
// Decodes one frame from a video with the FFmpeg libraries that ship with OBS (mp4, mkv, mov, flv, ...: whatever OBS can
// record). Returns a null image if FFmpeg is not available in this build or the file cannot be decoded.
QImage ffmpeg_thumbnail(const QString &path, int size);
}
