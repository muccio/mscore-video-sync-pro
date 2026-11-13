#include "FilmstripGenerator.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace MScoreVideoSync
{

FilmstripGenerator::FilmstripGenerator()
    : juce::Thread("FilmstripGeneratorThread")
{
}

FilmstripGenerator::~FilmstripGenerator()
{
    cancel();
}

void FilmstripGenerator::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void FilmstripGenerator::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void FilmstripGenerator::cancel()
{
    signalThreadShouldExit();
    stopThread(2000);

    std::lock_guard<std::mutex> lock(cacheMutex_);
    thumbnailCache_.clear();
    isComplete_.store(false, std::memory_order_release);
    progress_.store(0.0f, std::memory_order_release);
}

void FilmstripGenerator::startExtraction(const juce::File& videoFile, double durationSeconds, double intervalSeconds)
{
    cancel();
    currentFile_ = videoFile;
    durationSeconds_ = durationSeconds;
    const double baseInterval = std::max(0.5, intervalSeconds);
    intervalSeconds_ = (durationSeconds_ > 0.0)
        ? std::max(baseInterval, durationSeconds_ / 120.0)
        : baseInterval;
    isComplete_.store(false, std::memory_order_release);
    progress_.store(0.0f, std::memory_order_release);

    startThread(juce::Thread::Priority::background);
}

juce::Image FilmstripGenerator::getThumbnailForTime(double timeSeconds, double interval) const
{
    const double effInterval = (interval > 0.0) ? interval : intervalSeconds_;
    int index = static_cast<int>(std::floor(timeSeconds / effInterval));
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = thumbnailCache_.find(index);
    if (it != thumbnailCache_.end())
    {
        return it->second;
    }
    return {};
}

void FilmstripGenerator::run()
{
    if (!currentFile_.existsAsFile() || durationSeconds_ <= 0.0)
        return;

    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, currentFile_.getFullPathName().toRawUTF8(), nullptr, nullptr) < 0)
        return;

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0)
    {
        avformat_close_input(&fmtCtx);
        return;
    }

    int vStreamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vStreamIdx < 0)
    {
        avformat_close_input(&fmtCtx);
        return;
    }

    AVStream* vStream = fmtCtx->streams[vStreamIdx];
    const AVCodec* codec = avcodec_find_decoder(vStream->codecpar->codec_id);
    if (!codec)
    {
        avformat_close_input(&fmtCtx);
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx || avcodec_parameters_to_context(codecCtx, vStream->codecpar) < 0 || avcodec_open2(codecCtx, codec, nullptr) < 0)
    {
        if (codecCtx) avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    const int thumbW = 120;
    const int thumbH = 68;
    SwsContext* sws = nullptr;

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    if (!frame || !pkt)
    {
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    int totalIntervals = std::max(1, static_cast<int>(std::ceil(durationSeconds_ / intervalSeconds_)));

    for (int i = 0; i < totalIntervals && !threadShouldExit(); ++i)
    {
        double targetSec = i * intervalSeconds_;
        int64_t ts = static_cast<int64_t>(targetSec / av_q2d(vStream->time_base));
        av_seek_frame(fmtCtx, vStreamIdx, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codecCtx);

        bool frameFound = false;
        int packetsRead = 0;
        while (!frameFound && !threadShouldExit() && packetsRead < 240)
        {
            if (av_read_frame(fmtCtx, pkt) < 0)
                break;

            ++packetsRead;
            if (pkt->stream_index == vStreamIdx)
            {
                if (avcodec_send_packet(codecCtx, pkt) == 0)
                {
                    if (avcodec_receive_frame(codecCtx, frame) == 0)
                    {
                        if (frame->width > 0 && frame->height > 0)
                        {
                            sws = sws_getCachedContext(sws,
                                                       frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                                       thumbW, thumbH, AV_PIX_FMT_BGRA,
                                                       SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                            if (sws)
                            {
                                juce::Image thumb(juce::Image::ARGB, thumbW, thumbH, false);
                                juce::Image::BitmapData bdata(thumb, juce::Image::BitmapData::writeOnly);

                                uint8_t* dst[4] = { bdata.data, nullptr, nullptr, nullptr };
                                int dstStride[4] = { bdata.lineStride, 0, 0, 0 };

                                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);

                                {
                                    std::lock_guard<std::mutex> lock(cacheMutex_);
                                    thumbnailCache_[i] = thumb;
                                }
                            }
                        }
                        av_frame_unref(frame);
                        frameFound = true;
                    }
                }
            }
            av_packet_unref(pkt);
        }

        progress_.store(static_cast<float>(i + 1) / totalIntervals, std::memory_order_relaxed);
        if ((i % 8) == 0)
            listeners_.call([](Listener& l) { l.filmstripUpdated(); });
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (sws) sws_freeContext(sws);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    isComplete_.store(true, std::memory_order_release);
    listeners_.call([](Listener& l) { l.filmstripUpdated(); });
}

} // namespace MScoreVideoSync
