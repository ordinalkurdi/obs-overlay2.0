#include "video_thumb.hpp"
#include <algorithm>

#ifdef OVL_HAVE_FFMPEG_HEADERS

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}
#include <QLibrary>

namespace ovl {
namespace {

// Function table resolved from OBS's own DLLs. Only a handful of stable entry points and struct fields that sit at the
// start of their structs (and have kept their place across FFmpeg 7 and 8) are used.
struct Api {
	QLibrary util, codec, format, scale;
	bool ok = false;
	decltype(&avformat_open_input) open_input = nullptr;
	decltype(&avformat_find_stream_info) find_info = nullptr;
	decltype(&av_find_best_stream) best_stream = nullptr;
	decltype(&av_seek_frame) seek = nullptr;
	decltype(&av_read_frame) read_frame = nullptr;
	decltype(&avformat_close_input) close_input = nullptr;
	decltype(&avcodec_find_decoder) find_decoder = nullptr;
	decltype(&avcodec_alloc_context3) alloc_ctx = nullptr;
	decltype(&avcodec_parameters_to_context) params_to_ctx = nullptr;
	decltype(&avcodec_open2) open_codec = nullptr;
	decltype(&avcodec_flush_buffers) flush = nullptr;
	decltype(&avcodec_send_packet) send_packet = nullptr;
	decltype(&avcodec_receive_frame) receive_frame = nullptr;
	decltype(&avcodec_free_context) free_ctx = nullptr;
	decltype(&av_packet_alloc) packet_alloc = nullptr;
	decltype(&av_packet_unref) packet_unref = nullptr;
	decltype(&av_packet_free) packet_free = nullptr;
	decltype(&av_frame_alloc) frame_alloc = nullptr;
	decltype(&av_frame_free) frame_free = nullptr;
	decltype(&sws_getContext) sws_get = nullptr;
	decltype(&sws_scale) sws_run = nullptr;
	decltype(&sws_freeContext) sws_free = nullptr;
};

template<class T> bool resolve(QLibrary &lib, const char *name, T &fn)
{
	fn = reinterpret_cast<T>(lib.resolve(name));
	return fn != nullptr;
}

bool load_set(Api &a, int util, int codec, int format, int scale)
{
	a.util.setFileName(QString("avutil-%1").arg(util));
	a.codec.setFileName(QString("avcodec-%1").arg(codec));
	a.format.setFileName(QString("avformat-%1").arg(format));
	a.scale.setFileName(QString("swscale-%1").arg(scale));
	if (!a.util.load() || !a.codec.load() || !a.format.load() || !a.scale.load()) return false;
	return resolve(a.format, "avformat_open_input", a.open_input) && resolve(a.format, "avformat_find_stream_info", a.find_info) &&
	       resolve(a.format, "av_find_best_stream", a.best_stream) && resolve(a.format, "av_seek_frame", a.seek) &&
	       resolve(a.format, "av_read_frame", a.read_frame) && resolve(a.format, "avformat_close_input", a.close_input) &&
	       resolve(a.codec, "avcodec_find_decoder", a.find_decoder) && resolve(a.codec, "avcodec_alloc_context3", a.alloc_ctx) &&
	       resolve(a.codec, "avcodec_parameters_to_context", a.params_to_ctx) && resolve(a.codec, "avcodec_open2", a.open_codec) &&
	       resolve(a.codec, "avcodec_flush_buffers", a.flush) && resolve(a.codec, "avcodec_send_packet", a.send_packet) &&
	       resolve(a.codec, "avcodec_receive_frame", a.receive_frame) && resolve(a.codec, "avcodec_free_context", a.free_ctx) &&
	       resolve(a.codec, "av_packet_alloc", a.packet_alloc) && resolve(a.codec, "av_packet_unref", a.packet_unref) &&
	       resolve(a.codec, "av_packet_free", a.packet_free) && resolve(a.util, "av_frame_alloc", a.frame_alloc) &&
	       resolve(a.util, "av_frame_free", a.frame_free) && resolve(a.scale, "sws_getContext", a.sws_get) &&
	       resolve(a.scale, "sws_scale", a.sws_run) && resolve(a.scale, "sws_freeContext", a.sws_free);
}

Api &api()
{
	static Api a = [] {
		Api x;
		// OBS 31/32 ship FFmpeg 7.x (avformat-61), newer builds FFmpeg 8 (avformat-62). Anything else: no thumbnails.
		x.ok = load_set(x, 60, 62, 62, 9) || load_set(x, 59, 61, 61, 8);
		return x;
	}();
	return a;
}

} // namespace

QImage ffmpeg_thumbnail(const QString &path, int size)
{
	Api &f = api();
	if (!f.ok) return {};
	AVFormatContext *fmt = nullptr;
	const QByteArray utf8 = path.toUtf8();
	if (f.open_input(&fmt, utf8.constData(), nullptr, nullptr) < 0) return {};

	QImage out;
	AVCodecContext *dec = nullptr;
	AVFrame *frame = f.frame_alloc();
	AVPacket *pkt = f.packet_alloc();
	SwsContext *sws = nullptr;

	do {
		if (!frame || !pkt || f.find_info(fmt, nullptr) < 0) break;
		const int idx = f.best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (idx < 0) break;
		const AVCodec *codec = f.find_decoder(fmt->streams[idx]->codecpar->codec_id);
		if (!codec) break;
		dec = f.alloc_ctx(codec);
		if (!dec || f.params_to_ctx(dec, fmt->streams[idx]->codecpar) < 0 || f.open_codec(dec, codec, nullptr) < 0) break;

		// the very first frame is often black: try ~2 s in first, then from the start for very short clips
		bool got = false;
		for (int attempt = 0; attempt < 2 && !got; ++attempt) {
			const int64_t t = attempt == 0 ? 2 * (int64_t)AV_TIME_BASE : 0;
			if (f.seek(fmt, -1, t, AVSEEK_FLAG_BACKWARD) >= 0) f.flush(dec);
			for (int guard = 0; !got && guard < 600 && f.read_frame(fmt, pkt) >= 0; ++guard) {
				if (pkt->stream_index == idx && f.send_packet(dec, pkt) >= 0 && f.receive_frame(dec, frame) == 0) got = true;
				f.packet_unref(pkt);
			}
		}
		if (!got || frame->width <= 0 || frame->height <= 0) break;

		const double k = std::min(1.0, std::min((double)size / frame->width, (double)size / frame->height));
		const int tw = std::max(2, (int)(frame->width * k)), th = std::max(2, (int)(frame->height * k));
		sws = f.sws_get(frame->width, frame->height, (AVPixelFormat)frame->format, tw, th, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (!sws) break;
		QImage img(tw, th, QImage::Format_RGB32); // BGRA in memory
		uint8_t *dst[1] = {img.bits()};
		int stride[1] = {(int)img.bytesPerLine()};
		f.sws_run(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
		out = img;
	} while (false);

	if (sws) f.sws_free(sws);
	if (dec) f.free_ctx(&dec);
	if (pkt) f.packet_free(&pkt);
	if (frame) f.frame_free(&frame);
	f.close_input(&fmt);
	return out;
}

} // namespace ovl

#else

namespace ovl {
QImage ffmpeg_thumbnail(const QString &, int) { return {}; }
}

#endif
