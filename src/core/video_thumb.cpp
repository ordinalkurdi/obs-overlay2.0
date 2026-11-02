#include "video_thumb.hpp"
#include <algorithm>

#ifdef OVL_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}
#endif

namespace ovl {

#ifdef OVL_HAVE_FFMPEG

QImage ffmpeg_thumbnail(const QString &path, int size)
{
	AVFormatContext *fmt = nullptr;
	const QByteArray utf8 = path.toUtf8();
	if (avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr) < 0) return {};

	QImage out;
	AVCodecContext *dec = nullptr;
	AVFrame *frame = av_frame_alloc();
	AVPacket *pkt = av_packet_alloc();
	SwsContext *sws = nullptr;

	do {
		if (avformat_find_stream_info(fmt, nullptr) < 0) break;
		const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (idx < 0) break;
		AVStream *st = fmt->streams[idx];
		const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
		if (!codec) break;
		dec = avcodec_alloc_context3(codec);
		if (!dec || avcodec_parameters_to_context(dec, st->codecpar) < 0 || avcodec_open2(dec, codec, nullptr) < 0) break;

		// the very first frame is often black: look ~10% into the video (at most 5 s)
		if (fmt->duration > 0) {
			const int64_t t = std::min<int64_t>(fmt->duration / 10, 5 * (int64_t)AV_TIME_BASE);
			if (av_seek_frame(fmt, -1, t, AVSEEK_FLAG_BACKWARD) >= 0) avcodec_flush_buffers(dec);
		}

		bool got = false;
		for (int guard = 0; !got && guard < 600 && av_read_frame(fmt, pkt) >= 0; ++guard) {
			if (pkt->stream_index == idx && avcodec_send_packet(dec, pkt) >= 0 && avcodec_receive_frame(dec, frame) == 0) got = true;
			av_packet_unref(pkt);
		}
		if (!got || frame->width <= 0 || frame->height <= 0) break;

		const double k = std::min(1.0, std::min((double)size / frame->width, (double)size / frame->height));
		const int tw = std::max(2, (int)(frame->width * k)), th = std::max(2, (int)(frame->height * k));
		sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, tw, th, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (!sws) break;
		QImage img(tw, th, QImage::Format_RGB32); // BGRA in memory
		uint8_t *dst[1] = {img.bits()};
		int stride[1] = {(int)img.bytesPerLine()};
		sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
		out = img;
	} while (false);

	if (sws) sws_freeContext(sws);
	if (dec) avcodec_free_context(&dec);
	av_packet_free(&pkt);
	av_frame_free(&frame);
	avformat_close_input(&fmt);
	return out;
}

#else

QImage ffmpeg_thumbnail(const QString &, int) { return {}; }

#endif

} // namespace ovl
