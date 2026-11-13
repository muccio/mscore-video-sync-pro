#include "EmbeddedAudioPlayer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace MScoreVideoSync
{

EmbeddedAudioPlayer::EmbeddedAudioPlayer()
    : juce::Thread("EmbeddedAudioThread")
{
}

EmbeddedAudioPlayer::~EmbeddedAudioPlayer()
{
    closeFile();
}

void EmbeddedAudioPlayer::prepareToPlay(double sampleRate) noexcept
{
    const double validRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    pendingSampleRate_.store(validRate, std::memory_order_release);
    needsResamplerReinit_.store(true, std::memory_order_release);
    resetFifo();
    notify();
}

void EmbeddedAudioPlayer::resetFifo() noexcept
{
    fifo_.reset();
    fifoBuffer_.clear();
}

bool EmbeddedAudioPlayer::initResamplerLocked(double targetRate)
{
    if (!codecCtx_)
        return false;

    if (swrCtx_)
    {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }

    hostSampleRate_ = (targetRate > 0.0) ? targetRate : 44100.0;

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, 2);

    const int ret = swr_alloc_set_opts2(&swrCtx_,
                                        &outLayout,
                                        AV_SAMPLE_FMT_FLTP,
                                        static_cast<int>(hostSampleRate_),
                                        &codecCtx_->ch_layout,
                                        codecCtx_->sample_fmt,
                                        codecCtx_->sample_rate,
                                        0, nullptr);
    av_channel_layout_uninit(&outLayout);

    if (ret < 0 || !swrCtx_ || swr_init(swrCtx_) < 0)
    {
        if (swrCtx_)
        {
            swr_free(&swrCtx_);
            swrCtx_ = nullptr;
        }
        return false;
    }
    return true;
}

bool EmbeddedAudioPlayer::openFile(const juce::File& videoFile, double targetSampleRate)
{
    // Stop worker thread and release previous contexts BEFORE locking fileMutex_
    // to avoid self-deadlock with closeFile().
    closeFile();

    if (!videoFile.existsAsFile())
        return false;

    std::lock_guard<std::mutex> lock(fileMutex_);

    const double requestedRate = (targetSampleRate > 0.0)
        ? targetSampleRate
        : pendingSampleRate_.load(std::memory_order_acquire);
    pendingSampleRate_.store(requestedRate, std::memory_order_release);
    needsResamplerReinit_.store(false, std::memory_order_release);

    int ret = avformat_open_input(&formatCtx_, videoFile.getFullPathName().toRawUTF8(), nullptr, nullptr);
    if (ret < 0) return false;

    if (avformat_find_stream_info(formatCtx_, nullptr) < 0)
    {
        cleanupFFmpeg();
        return false;
    }

    audioStreamIndex_ = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStreamIndex_ < 0)
    {
        cleanupFFmpeg();
        return false;
    }

    audioStream_ = formatCtx_->streams[audioStreamIndex_];
    const AVCodec* codec = avcodec_find_decoder(audioStream_->codecpar->codec_id);
    if (!codec)
    {
        cleanupFFmpeg();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_ || avcodec_parameters_to_context(codecCtx_, audioStream_->codecpar) < 0)
    {
        cleanupFFmpeg();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0)
    {
        cleanupFFmpeg();
        return false;
    }

    if (!initResamplerLocked(requestedRate))
    {
        cleanupFFmpeg();
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_)
    {
        cleanupFFmpeg();
        return false;
    }

    resetFifo();

    isLoaded_.store(true, std::memory_order_release);
    startThread(juce::Thread::Priority::normal);
    return true;
}

void EmbeddedAudioPlayer::closeFile()
{
    isLoaded_.store(false, std::memory_order_release);
    seekRequested_.store(false, std::memory_order_release);
    prevPlaybackTargetPts_.store(-1.0, std::memory_order_release);
    signalThreadShouldExit();
    notify();
    stopThread(2000);

    std::lock_guard<std::mutex> lock(fileMutex_);
    cleanupFFmpeg();
    resetFifo();
}

void EmbeddedAudioPlayer::cleanupFFmpeg()
{
    if (swrCtx_)
    {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }
    if (frame_)
    {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (packet_)
    {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (codecCtx_)
    {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (formatCtx_)
    {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }
    audioStreamIndex_ = -1;
    audioStream_ = nullptr;
}

void EmbeddedAudioPlayer::setPlaybackState(bool isPlaying, double targetPts)
{
    targetPts = std::max(0.0, targetPts);
    const bool wasPlaying = isPlaying_.exchange(isPlaying, std::memory_order_acq_rel);
    const double prevTarget = prevPlaybackTargetPts_.exchange(targetPts, std::memory_order_acq_rel);
    targetPts_.store(targetPts, std::memory_order_release);

    if (!wasPlaying && isPlaying)
    {
        requestSeek(targetPts);
    }
    else if (isPlaying && prevTarget >= 0.0)
    {
        const double stepDelta = targetPts - prevTarget;
        if (stepDelta < -0.05 || stepDelta > 0.25)
        {
            requestSeek(targetPts);
        }
    }
}

void EmbeddedAudioPlayer::requestSeek(double targetPts)
{
    seekTargetPts_.store(targetPts, std::memory_order_release);
    seekRequested_.store(true, std::memory_order_release);
    notify();
}

void EmbeddedAudioPlayer::setGainDecibels(float gainDb) noexcept
{
    gainDb_.store(gainDb, std::memory_order_relaxed);
    if (gainDb <= -60.0f)
    {
        linearGain_.store(0.0f, std::memory_order_relaxed);
    }
    else
    {
        linearGain_.store(juce::Decibels::decibelsToGain(gainDb), std::memory_order_relaxed);
    }
}

void EmbeddedAudioPlayer::setMuted(bool muted) noexcept
{
    isMuted_.store(muted, std::memory_order_relaxed);
}

void EmbeddedAudioPlayer::setDimmed(bool dimmed) noexcept
{
    isDimmed_.store(dimmed, std::memory_order_relaxed);
}

template <typename FloatType>
void EmbeddedAudioPlayer::processBlockAdding(juce::AudioBuffer<FloatType>& buffer, int numSamples) noexcept
{
    if (!isLoaded_.load(std::memory_order_relaxed) || !isPlaying_.load(std::memory_order_relaxed))
    {
        peakLeft_.store(0.0f, std::memory_order_relaxed);
        peakRight_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    if (isMuted_.load(std::memory_order_relaxed))
    {
        // Pull and discard to maintain sync
        int start1, size1, start2, size2;
        fifo_.prepareToRead(numSamples, start1, size1, start2, size2);
        fifo_.finishedRead(size1 + size2);
        peakLeft_.store(0.0f, std::memory_order_relaxed);
        peakRight_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    float gain = linearGain_.load(std::memory_order_relaxed);
    if (isDimmed_.load(std::memory_order_relaxed))
    {
        gain *= 0.17782794f; // -15 dB
    }

    int start1, size1, start2, size2;
    fifo_.prepareToRead(numSamples, start1, size1, start2, size2);

    float maxL = 0.0f;
    float maxR = 0.0f;

    auto readChunk = [&](int fifoStart, int destStart, int count) {
        for (int ch = 0; ch < std::min(2, buffer.getNumChannels()); ++ch)
        {
            const float* src = fifoBuffer_.getReadPointer(ch, fifoStart);
            for (int i = 0; i < count; ++i)
            {
                float val = src[i] * gain;
                buffer.addSample(ch, destStart + i, static_cast<FloatType>(val));
                if (ch == 0 && std::abs(val) > maxL) maxL = std::abs(val);
                if (ch == 1 && std::abs(val) > maxR) maxR = std::abs(val);
            }
        }
    };

    if (size1 > 0) readChunk(start1, 0, size1);
    if (size2 > 0) readChunk(start2, size1, size2);

    fifo_.finishedRead(size1 + size2);

    peakLeft_.store(maxL, std::memory_order_relaxed);
    peakRight_.store(maxR, std::memory_order_relaxed);
}

template void EmbeddedAudioPlayer::processBlockAdding<float>(juce::AudioBuffer<float>&, int) noexcept;
template void EmbeddedAudioPlayer::processBlockAdding<double>(juce::AudioBuffer<double>&, int) noexcept;

void EmbeddedAudioPlayer::run()
{
    while (!threadShouldExit())
    {
        if (!isLoaded_.load(std::memory_order_acquire))
        {
            wait(50);
            continue;
        }

        if (needsResamplerReinit_.exchange(false, std::memory_order_acq_rel))
        {
            std::lock_guard<std::mutex> lock(fileMutex_);
            const double targetRate = pendingSampleRate_.load(std::memory_order_acquire);
            if (std::abs(targetRate - hostSampleRate_) > 0.5)
            {
                initResamplerLocked(targetRate);
                resetFifo();
            }
        }

        if (seekRequested_.exchange(false, std::memory_order_acq_rel))
        {
            double target = std::max(0.0, seekTargetPts_.load(std::memory_order_acquire));

            std::lock_guard<std::mutex> lock(fileMutex_);
            if (formatCtx_ && audioStreamIndex_ >= 0)
            {
                int64_t ts = static_cast<int64_t>(target / av_q2d(audioStream_->time_base));
                av_seek_frame(formatCtx_, audioStreamIndex_, ts, AVSEEK_FLAG_BACKWARD);
                if (codecCtx_) avcodec_flush_buffers(codecCtx_);
                resetFifo();
            }
            continue;
        }

        // Keep FIFO filled to at least 16384 samples (~370ms buffer)
        if (fifo_.getNumReady() < 16384)
        {
            bool eof = false;
            if (!decodeAudioPacket(eof))
            {
                wait(eof ? 15 : 2);
            }
        }
        else
        {
            wait(5);
        }
    }
}

bool EmbeddedAudioPlayer::decodeAudioPacket(bool& reachedEof)
{
    reachedEof = false;
    std::lock_guard<std::mutex> lock(fileMutex_);

    if (!formatCtx_ || !codecCtx_ || !swrCtx_ || audioStreamIndex_ < 0)
        return false;

    while (av_read_frame(formatCtx_, packet_) >= 0)
    {
        if (packet_->stream_index == audioStreamIndex_)
        {
            int ret = avcodec_send_packet(codecCtx_, packet_);
            av_packet_unref(packet_);

            if (ret < 0) return false;

            while (avcodec_receive_frame(codecCtx_, frame_) >= 0)
            {
                int maxOutSamples = swr_get_out_samples(swrCtx_, frame_->nb_samples);
                if (maxOutSamples > 0)
                {
                    float outL[4096];
                    float outR[4096];
                    uint8_t* outData[2] = { reinterpret_cast<uint8_t*>(outL), reinterpret_cast<uint8_t*>(outR) };

                    int converted = swr_convert(swrCtx_,
                                                outData,
                                                std::min(maxOutSamples, 4096),
                                                const_cast<const uint8_t**>(frame_->extended_data),
                                                frame_->nb_samples);

                    if (converted > 0)
                    {
                        int start1, size1, start2, size2;
                        fifo_.prepareToWrite(converted, start1, size1, start2, size2);

                        if (size1 > 0)
                        {
                            fifoBuffer_.copyFrom(0, start1, outL, size1);
                            fifoBuffer_.copyFrom(1, start1, outR, size1);
                        }
                        if (size2 > 0)
                        {
                            fifoBuffer_.copyFrom(0, start2, outL + size1, size2);
                            fifoBuffer_.copyFrom(1, start2, outR + size1, size2);
                        }
                        fifo_.finishedWrite(size1 + size2);
                    }
                }
                av_frame_unref(frame_);
            }
            return true;
        }
        av_packet_unref(packet_);
    }

    reachedEof = true;
    return false;
}

} // namespace MScoreVideoSync
