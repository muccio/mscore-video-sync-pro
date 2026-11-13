#include "WaveformExtractor.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

namespace MScoreVideoSync
{

WaveformExtractor::WaveformExtractor()
    : juce::Thread("WaveformExtractorThread")
{
}

WaveformExtractor::~WaveformExtractor()
{
    cancel();
}

void WaveformExtractor::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void WaveformExtractor::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void WaveformExtractor::cancel()
{
    signalThreadShouldExit();
    stopThread(2000);

    std::lock_guard<std::mutex> lock(peakMutex_);
    peaks_.clear();
    isComplete_.store(false, std::memory_order_release);
}

void WaveformExtractor::startExtraction(const juce::File& videoFile, double durationSeconds, double timeResolution)
{
    cancel();
    currentFile_ = videoFile;
    durationSeconds_ = durationSeconds;
    timeResolution_ = std::max(0.01, timeResolution);
    isComplete_.store(false, std::memory_order_release);

    startThread(juce::Thread::Priority::background);
}

std::vector<WaveformExtractor::PeakPoint> WaveformExtractor::getPeaksSnapshot() const
{
    std::lock_guard<std::mutex> lock(peakMutex_);
    return peaks_;
}

void WaveformExtractor::run()
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

    int aStreamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (aStreamIdx < 0)
    {
        avformat_close_input(&fmtCtx);
        return;
    }

    AVStream* aStream = fmtCtx->streams[aStreamIdx];
    const AVCodec* codec = avcodec_find_decoder(aStream->codecpar->codec_id);
    if (!codec)
    {
        avformat_close_input(&fmtCtx);
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx || avcodec_parameters_to_context(codecCtx, aStream->codecpar) < 0 || avcodec_open2(codecCtx, codec, nullptr) < 0)
    {
        if (codecCtx) avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    // Downsample to 8000 Hz mono for fast peak calculation
    const int targetSr = 8000;
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, 1);

    SwrContext* swr = nullptr;
    const int swrRet = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, targetSr,
                                           &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,
                                           0, nullptr);
    av_channel_layout_uninit(&outLayout);

    if (swrRet < 0 || !swr || swr_init(swr) < 0)
    {
        if (swr) swr_free(&swr);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    if (!frame || !pkt)
    {
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    int samplesPerBucket = static_cast<int>(targetSr * timeResolution_);
    if (samplesPerBucket < 1) samplesPerBucket = 1;

    std::vector<PeakPoint> localPeaks;
    float currentMin = 0.0f;
    float currentMax = 0.0f;
    int bucketSampleCount = 0;

    float resampleBuf[2048];
    uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(resampleBuf) };

    while (av_read_frame(fmtCtx, pkt) >= 0 && !threadShouldExit())
    {
        if (pkt->stream_index == aStreamIdx)
        {
            if (avcodec_send_packet(codecCtx, pkt) == 0)
            {
                while (avcodec_receive_frame(codecCtx, frame) == 0)
                {
                    int outSamples = swr_convert(swr, outData, 2048,
                                                 const_cast<const uint8_t**>(frame->extended_data),
                                                 frame->nb_samples);

                    for (int s = 0; s < outSamples; ++s)
                    {
                        float sample = resampleBuf[s];
                        if (sample < currentMin) currentMin = sample;
                        if (sample > currentMax) currentMax = sample;

                        bucketSampleCount++;
                        if (bucketSampleCount >= samplesPerBucket)
                        {
                            localPeaks.push_back({ currentMin, currentMax });
                            currentMin = 0.0f;
                            currentMax = 0.0f;
                            bucketSampleCount = 0;
                        }
                    }
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (bucketSampleCount > 0)
    {
        localPeaks.push_back({ currentMin, currentMax });
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    {
        std::lock_guard<std::mutex> lock(peakMutex_);
        peaks_ = std::move(localPeaks);
    }

    isComplete_.store(true, std::memory_order_release);
    listeners_.call([](Listener& l) { l.waveformReady(); });
}

} // namespace MScoreVideoSync
