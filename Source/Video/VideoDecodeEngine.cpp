#include "VideoDecodeEngine.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_videotoolbox.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
}

namespace MScoreVideoSync
{

static enum AVPixelFormat get_hw_format_callback(AVCodecContext* /*ctx*/, const enum AVPixelFormat* pix_fmts)
{
    const enum AVPixelFormat* p;
    for (p = pix_fmts; *p != -1; p++)
    {
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX)
        {
            return *p;
        }
    }
    return pix_fmts[0];
}

VideoDecodeEngine::VideoDecodeEngine()
    : juce::Thread("VideoDecodeEngineThread", 0)
{
    startThread(juce::Thread::Priority::high);
}

VideoDecodeEngine::~VideoDecodeEngine()
{
    signalThreadShouldExit();
    notify();
    stopThread(3000);

    closeFile();
}

void VideoDecodeEngine::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void VideoDecodeEngine::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

bool VideoDecodeEngine::openFile(const juce::File& videoFile)
{
    closeFile();

    if (!videoFile.existsAsFile())
    {
        const juce::String msg = "File does not exist: " + videoFile.getFullPathName();
        listeners_.call([&](Listener& l) { l.videoLoadFailed(msg); });
        return false;
    }

    VideoMetadata metaCopy;
    juce::String failMessage;

    {
        std::lock_guard<std::mutex> lock(fileMutex_);

        juce::String pathUtf8 = videoFile.getFullPathName();
        int ret = avformat_open_input(&formatCtx_, pathUtf8.toRawUTF8(), nullptr, nullptr);
        if (ret < 0)
        {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
            av_strerror(ret, errBuf, sizeof(errBuf));
            failMessage = "Cannot open video file: " + juce::String(errBuf);
        }
        else if (avformat_find_stream_info(formatCtx_, nullptr) < 0)
        {
            failMessage = "Cannot find stream information";
            cleanupFFmpeg();
        }
        else if ((videoStreamIndex_ = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0)) < 0)
        {
            failMessage = "No valid video stream found in file";
            cleanupFFmpeg();
        }
        else
        {
            videoStream_ = formatCtx_->streams[videoStreamIndex_];
            initHardwareContext();

            if (!initCodecContext())
            {
                failMessage = "Failed to initialize video codec context";
                cleanupFFmpeg();
            }
            else
            {
                hwFrame_ = av_frame_alloc();
                swFrame_ = av_frame_alloc();
                packet_ = av_packet_alloc();

                if (!hwFrame_ || !swFrame_ || !packet_)
                {
                    failMessage = "Failed to allocate FFmpeg frame structures";
                    cleanupFFmpeg();
                }
                else
                {
                    metadata_.isValid = true;
                    metadata_.filePath = videoFile.getFullPathName();
                    metadata_.width = codecCtx_->width;
                    metadata_.height = codecCtx_->height;

                    if (formatCtx_->duration != AV_NOPTS_VALUE)
                    {
                        metadata_.durationSeconds = static_cast<double>(formatCtx_->duration) / AV_TIME_BASE;
                    }
                    else if (videoStream_->duration != AV_NOPTS_VALUE)
                    {
                        metadata_.durationSeconds = static_cast<double>(videoStream_->duration) * av_q2d(videoStream_->time_base);
                    }
                    else
                    {
                        metadata_.durationSeconds = 0.0;
                    }

                    AVRational fr = av_guess_frame_rate(formatCtx_, videoStream_, nullptr);
                    metadata_.frameRate = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 25.0;

                    const AVCodec* codec = codecCtx_->codec;
                    metadata_.codecName = codec ? codec->name : "Unknown";
                    metadata_.isHardwareAccelerated = (hwDeviceCtx_ != nullptr);
                    const char* pixName = av_get_pix_fmt_name(codecCtx_->pix_fmt);
                    metadata_.pixelFormatName = pixName ? pixName : "Unknown";

                    int aIdx = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
                    if (aIdx >= 0)
                    {
                        metadata_.hasAudioStream = true;
                        auto* as = formatCtx_->streams[aIdx];
                        metadata_.audioSampleRate = as->codecpar->sample_rate;
                        metadata_.audioChannels = as->codecpar->ch_layout.nb_channels;
                        const AVCodec* acodec = avcodec_find_decoder(as->codecpar->codec_id);
                        metadata_.audioCodecName = acodec ? acodec->name : "Unknown";
                    }

                    frameQueue_.prepareBuffers(metadata_.width, metadata_.height);

                    isHwAccelActive_.store(metadata_.isHardwareAccelerated, std::memory_order_release);
                    isLoaded_.store(true, std::memory_order_release);
                    metaCopy = metadata_;
                }
            }
        }
    }

    if (failMessage.isNotEmpty())
    {
        listeners_.call([&](Listener& l) { l.videoLoadFailed(failMessage); });
        return false;
    }

    if (!isThreadRunning())
    {
        startThread(juce::Thread::Priority::high);
    }

    // Trigger initial frame
    lastSeekTargetPts_.store(0.0, std::memory_order_release);
    seekTargetPts_.store(0.0, std::memory_order_release);
    seekRequested_.store(true, std::memory_order_release);
    notify();

    // Start background thumbnail & waveform generators
    filmstripGenerator_.startExtraction(videoFile, metaCopy.durationSeconds, 2.0);
    if (metaCopy.hasAudioStream)
    {
        waveformExtractor_.startExtraction(videoFile, metaCopy.durationSeconds, 0.05);
    }

    listeners_.call([&](Listener& l) { l.videoLoaded(metaCopy); });
    return true;
}

void VideoDecodeEngine::closeFile()
{
    isLoaded_.store(false, std::memory_order_release);
    seekRequested_.store(false, std::memory_order_release);
    isSeeking_.store(false, std::memory_order_release);
    streamNeedsSeek_.store(false, std::memory_order_release);
    prevPlaybackTargetPts_.store(-1.0, std::memory_order_release);
    lastSeekTargetPts_.store(-1.0, std::memory_order_release);
    frameQueue_.clear();

    filmstripGenerator_.cancel();
    waveformExtractor_.cancel();

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        frameRingCache_.clear();
    }

    std::lock_guard<std::mutex> lock(fileMutex_);
    cleanupFFmpeg();
    metadata_ = VideoMetadata();
}

bool VideoDecodeEngine::initHardwareContext()
{
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    if (ret < 0)
    {
        hwDeviceCtx_ = nullptr;
        return false;
    }
    return true;
}

bool VideoDecodeEngine::initCodecContext()
{
    const AVCodec* codec = avcodec_find_decoder(videoStream_->codecpar->codec_id);
    if (!codec) return false;

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_ || avcodec_parameters_to_context(codecCtx_, videoStream_->codecpar) < 0)
        return false;

    if (hwDeviceCtx_ != nullptr)
    {
        codecCtx_->get_format = get_hw_format_callback;
        codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    }

    codecCtx_->thread_count = 0; // FFmpeg optimal multi-threading
    if (avcodec_open2(codecCtx_, codec, nullptr) < 0)
        return false;

    return true;
}

void VideoDecodeEngine::cleanupFFmpeg()
{
    if (swsCtx_)
    {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (packet_)
    {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (hwFrame_)
    {
        av_frame_free(&hwFrame_);
        hwFrame_ = nullptr;
    }
    if (swFrame_)
    {
        av_frame_free(&swFrame_);
        swFrame_ = nullptr;
    }
    if (codecCtx_)
    {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (hwDeviceCtx_)
    {
        av_buffer_unref(&hwDeviceCtx_);
        hwDeviceCtx_ = nullptr;
    }
    if (formatCtx_)
    {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }
    videoStreamIndex_ = -1;
    videoStream_ = nullptr;
}

void VideoDecodeEngine::setPlaybackState(bool isPlaying, double targetPts)
{
    targetPts = std::max(0.0, targetPts);
    const bool wasPlaying = isPlaying_.exchange(isPlaying, std::memory_order_acq_rel);
    const double prevTarget = prevPlaybackTargetPts_.exchange(targetPts, std::memory_order_acq_rel);
    targetPts_.store(targetPts, std::memory_order_release);

    const double currentPts = currentDecodedPts_.load(std::memory_order_acquire);
    const double frameDuration = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;
    const double threshold = std::max(0.012, frameDuration * 0.35);

    if (!wasPlaying && isPlaying)
    {
        if (streamNeedsSeek_.load(std::memory_order_acquire)
            || std::abs(targetPts - currentPts) > std::max(0.05, frameDuration * 1.5))
        {
            requestSeek(targetPts);
        }
        else
        {
            notify();
        }
    }
    else if (!isPlaying)
    {
        double lastTarget = lastSeekTargetPts_.load(std::memory_order_acquire);
        if (lastTarget < 0.0 || std::abs(targetPts - lastTarget) >= threshold)
        {
            requestSeek(targetPts);
        }
    }
    else
    {
        // Detect discontinuous host jumps (including MuseScore 4 delayed cursor locate on block 2+)
        const double stepDelta = (prevTarget >= 0.0) ? (targetPts - prevTarget) : 0.0;
        const bool discontinuousJump = (prevTarget >= 0.0) && (stepDelta < -0.05 || stepDelta > 0.25);

        if (discontinuousJump)
        {
            requestSeek(targetPts);
        }
        else if (!isSeeking_.load(std::memory_order_acquire) && !seekRequested_.load(std::memory_order_acquire))
        {
            const double delta = targetPts - currentPts;
            if (delta < -0.25 || delta > 1.0)
            {
                requestSeek(targetPts);
            }
            else
            {
                notify();
            }
        }
        else
        {
            // Keep in-flight seek running without canceling it on normal playback progression
            notify();
        }
    }
}

void VideoDecodeEngine::requestSeek(double targetPts)
{
    double target = std::max(0.0, targetPts);
    lastSeekTargetPts_.store(target, std::memory_order_release);
    seekTargetPts_.store(target, std::memory_order_release);
    isSeeking_.store(true, std::memory_order_release);
    seekRequested_.store(true, std::memory_order_release);
    notify();
}

const DecodedVideoFrame* VideoDecodeEngine::getLatestFrame() noexcept
{
    return frameQueue_.getLatestFrame();
}

VideoMetadata VideoDecodeEngine::getMetadata() const
{
    return metadata_;
}

bool VideoDecodeEngine::isFileLoaded() const noexcept
{
    return isLoaded_.load(std::memory_order_acquire);
}

bool VideoDecodeEngine::isHardwareAccelerated() const noexcept
{
    return isHwAccelActive_.load(std::memory_order_acquire);
}

double VideoDecodeEngine::getCurrentDecodedPts() const noexcept
{
    return currentDecodedPts_.load(std::memory_order_acquire);
}

bool VideoDecodeEngine::tryLookupCachedFrame(double targetPts, DecodedVideoFrame* dest)
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (frameRingCache_.empty()) return false;

    double frameDur = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;
    for (const auto& item : frameRingCache_)
    {
        if (std::abs(item.pts - targetPts) <= frameDur * 0.45)
        {
            if (dest)
            {
                dest->pts = item.pts;
                dest->duration = frameDur;
                dest->image = item.image.createCopy();
                dest->isValid = true;
            }
            return true;
        }
    }
    return false;
}

void VideoDecodeEngine::cacheFrame(double pts, const juce::Image& image)
{
    if (!image.isValid())
        return;

    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (frameRingCache_.size() >= MaxCachedFrames)
    {
        frameRingCache_.pop_front();
    }
    frameRingCache_.push_back({ pts, image.createCopy() });
}

void VideoDecodeEngine::run()
{
    while (!threadShouldExit())
    {
        if (!isLoaded_.load(std::memory_order_acquire))
        {
            wait(50);
            continue;
        }

        if (seekRequested_.exchange(false, std::memory_order_acq_rel))
        {
            double target = seekTargetPts_.load(std::memory_order_acquire);

            // Check ring cache first for instant scrubbing!
            DecodedVideoFrame* writeBuf = frameQueue_.getWriteBuffer();
            if (writeBuf && tryLookupCachedFrame(target, writeBuf))
            {
                currentDecodedPts_.store(writeBuf->pts, std::memory_order_release);
                frameQueue_.commitWriteBuffer();
                streamNeedsSeek_.store(true, std::memory_order_release);

                if (!isPlaying_.load(std::memory_order_acquire))
                {
                    isSeeking_.store(false, std::memory_order_release);
                    continue;
                }
            }

            performSeek(target);
            if (!seekRequested_.load(std::memory_order_acquire))
                isSeeking_.store(false, std::memory_order_release);
            continue;
        }

        bool playing = isPlaying_.load(std::memory_order_acquire);
        double target = targetPts_.load(std::memory_order_acquire);
        double current = currentDecodedPts_.load(std::memory_order_acquire);

        if (playing)
        {
            if (streamNeedsSeek_.load(std::memory_order_acquire))
            {
                performSeek(target);
                continue;
            }

            double frameDuration = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;
            if (current + (frameDuration * 0.5) < target)
            {
                bool eof = false;
                bool decoded = false;
                {
                    std::lock_guard<std::mutex> lock(fileMutex_);
                    decoded = decodeNextFrame(eof, -1.0);
                }
                if (!decoded)
                {
                    wait(eof ? 15 : 2);
                }
            }
            else
            {
                wait(2);
            }
        }
        else
        {
            wait(15);
        }
    }
}

bool VideoDecodeEngine::performSeek(double targetSeconds)
{
    std::lock_guard<std::mutex> lock(fileMutex_);

    if (!formatCtx_ || !codecCtx_ || videoStreamIndex_ < 0)
        return false;

    const double frameDuration = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;
    const double maxSeekable = (metadata_.durationSeconds > frameDuration)
        ? (metadata_.durationSeconds - frameDuration)
        : std::max(0.0, targetSeconds);
    const double clampedTarget = juce::jlimit(0.0, std::max(0.0, maxSeekable), targetSeconds);

    int64_t targetTimestamp = static_cast<int64_t>(clampedTarget / av_q2d(videoStream_->time_base));
    int ret = av_seek_frame(formatCtx_, videoStreamIndex_, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        ret = av_seek_frame(formatCtx_, videoStreamIndex_, targetTimestamp, 0);
    }

    avcodec_flush_buffers(codecCtx_);
    streamNeedsSeek_.store(false, std::memory_order_release);

    bool reachedEof = false;
    constexpr int maxDecodeSteps = 2000;

    for (int step = 0; step < maxDecodeSteps && !reachedEof && !threadShouldExit() && isLoaded_.load(std::memory_order_acquire); ++step)
    {
        if (seekRequested_.load(std::memory_order_acquire))
            return false;

        double effectiveTarget = clampedTarget;
        if (isPlaying_.load(std::memory_order_acquire))
        {
            const double liveTarget = targetPts_.load(std::memory_order_acquire);
            if (liveTarget > effectiveTarget && (liveTarget - effectiveTarget) < 2.0)
                effectiveTarget = std::min(liveTarget, std::max(effectiveTarget, maxSeekable));
        }

        if (!decodeNextFrame(reachedEof, effectiveTarget))
        {
            break;
        }

        double decodedPts = currentDecodedPts_.load(std::memory_order_relaxed);
        if (decodedPts >= effectiveTarget - (frameDuration * 0.5))
        {
            break;
        }
    }

    return true;
}

bool VideoDecodeEngine::decodeNextFrame(bool& reachedEof, double targetCommitPts)
{
    reachedEof = false;
    if (!formatCtx_ || !codecCtx_ || !packet_ || !hwFrame_ || !swFrame_)
        return false;

    bool flushSent = false;

    while (!threadShouldExit())
    {
        int ret = avcodec_receive_frame(codecCtx_, hwFrame_);
        if (ret == 0)
        {
            AVFrame* finalFrame = hwFrame_;

            if (hwFrame_->format == AV_PIX_FMT_VIDEOTOOLBOX)
            {
                if (av_hwframe_transfer_data(swFrame_, hwFrame_, 0) >= 0)
                {
                    swFrame_->pts = hwFrame_->pts;
                    swFrame_->pkt_dts = hwFrame_->pkt_dts;
                    finalFrame = swFrame_;
                }
                else
                {
                    av_frame_unref(hwFrame_);
                    av_frame_unref(swFrame_);
                    return false;
                }
            }

            double ptsSeconds = 0.0;
            if (finalFrame->pts != AV_NOPTS_VALUE)
            {
                ptsSeconds = static_cast<double>(finalFrame->pts) * av_q2d(videoStream_->time_base);
            }
            else if (finalFrame->pkt_dts != AV_NOPTS_VALUE)
            {
                ptsSeconds = static_cast<double>(finalFrame->pkt_dts) * av_q2d(videoStream_->time_base);
            }

            currentDecodedPts_.store(ptsSeconds, std::memory_order_release);

            double frameDuration = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;
            bool shouldCommit = (targetCommitPts < 0.0) || (ptsSeconds >= targetCommitPts - (frameDuration * 0.5));

            if (shouldCommit)
            {
                DecodedVideoFrame* writeBuf = frameQueue_.getWriteBuffer();
                if (writeBuf)
                {
                    writeBuf->pts = ptsSeconds;
                    writeBuf->duration = frameDuration;
                    convertFrameToImage(finalFrame, writeBuf);
                    writeBuf->isValid = writeBuf->image.isValid();
                    frameQueue_.commitWriteBuffer();

                    // Update predictive frame ring cache
                    cacheFrame(ptsSeconds, writeBuf->image);
                }
            }

            av_frame_unref(hwFrame_);
            av_frame_unref(swFrame_);
            return true;
        }
        else if (ret == AVERROR_EOF)
        {
            reachedEof = true;
            return false;
        }
        else if (ret == AVERROR(EAGAIN))
        {
            bool packetSent = false;
            while (av_read_frame(formatCtx_, packet_) >= 0)
            {
                if (packet_->stream_index == videoStreamIndex_)
                {
                    int sendRet = avcodec_send_packet(codecCtx_, packet_);
                    av_packet_unref(packet_);
                    packetSent = true;
                    if (sendRet < 0 && sendRet != AVERROR(EAGAIN))
                    {
                        return false;
                    }
                    break;
                }
                av_packet_unref(packet_);
            }

            if (!packetSent)
            {
                if (flushSent)
                {
                    reachedEof = true;
                    return false;
                }
                avcodec_send_packet(codecCtx_, nullptr);
                flushSent = true;
            }
        }
        else
        {
            return false;
        }
    }

    return false;
}

void VideoDecodeEngine::convertFrameToImage(AVFrame* srcFrame, DecodedVideoFrame* destFrame)
{
    int width = srcFrame->width;
    int height = srcFrame->height;

    if (width <= 0 || height <= 0) return;

    if (destFrame->image.getWidth() != width || destFrame->image.getHeight() != height)
    {
        destFrame->image = juce::Image(juce::Image::ARGB, width, height, true);
    }

    swsCtx_ = sws_getCachedContext(swsCtx_,
                                   width, height, static_cast<AVPixelFormat>(srcFrame->format),
                                   width, height, AV_PIX_FMT_BGRA,
                                   SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsCtx_) return;

    juce::Image::BitmapData destData(destFrame->image, juce::Image::BitmapData::writeOnly);
    uint8_t* destPointers[4] = { destData.data, nullptr, nullptr, nullptr };
    int destLineSizes[4] = { destData.lineStride, 0, 0, 0 };

    sws_scale(swsCtx_, srcFrame->data, srcFrame->linesize, 0, height, destPointers, destLineSizes);
}

} // namespace MScoreVideoSync
