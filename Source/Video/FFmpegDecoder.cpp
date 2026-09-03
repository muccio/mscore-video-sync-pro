#include "FFmpegDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_videotoolbox.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
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
    // Fallback to first available software pixel format
    return pix_fmts[0];
}

FFmpegDecoder::FFmpegDecoder()
    : juce::Thread("FFmpegDecoderThread", 0) // default stack size
{
    // Start thread with high priority for smooth real-time video decoding
    startThread(juce::Thread::Priority::high);
}

FFmpegDecoder::~FFmpegDecoder()
{
    signalThreadShouldExit();
    notify();
    stopThread(3000);

    closeFile();
}

void FFmpegDecoder::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void FFmpegDecoder::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

bool FFmpegDecoder::openFile(const juce::File& videoFile)
{
    std::lock_guard<std::mutex> lock(fileMutex_);

    closeFile();

    if (!videoFile.existsAsFile())
    {
        listeners_.call([&](Listener& l) { l.videoLoadFailed("File does not exist: " + videoFile.getFullPathName()); });
        return false;
    }

    juce::String pathUtf8 = videoFile.getFullPathName();
    int ret = avformat_open_input(&formatCtx_, pathUtf8.toRawUTF8(), nullptr, nullptr);
    if (ret < 0)
    {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        av_strerror(ret, errBuf, sizeof(errBuf));
        juce::String errStr = "Cannot open video file: " + juce::String(errBuf);
        listeners_.call([&](Listener& l) { l.videoLoadFailed(errStr); });
        return false;
    }

    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0)
    {
        listeners_.call([&](Listener& l) { l.videoLoadFailed("Cannot find stream information"); });
        cleanupFFmpeg();
        return false;
    }

    // Find best video stream
    videoStreamIndex_ = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0)
    {
        listeners_.call([&](Listener& l) { l.videoLoadFailed("No valid video stream found in file"); });
        cleanupFFmpeg();
        return false;
    }

    videoStream_ = formatCtx_->streams[videoStreamIndex_];

    if (!initHardwareContext())
    {
        DBG("VideoToolbox hardware device context init failed, proceeding with software decoding.");
    }

    if (!initCodecContext())
    {
        listeners_.call([&](Listener& l) { l.videoLoadFailed("Failed to initialize video codec context"); });
        cleanupFFmpeg();
        return false;
    }

    // Allocate frame structures
    hwFrame_ = av_frame_alloc();
    swFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();

    if (!hwFrame_ || !swFrame_ || !packet_)
    {
        listeners_.call([&](Listener& l) { l.videoLoadFailed("Failed to allocate FFmpeg frame structures"); });
        cleanupFFmpeg();
        return false;
    }

    // Calculate metadata
    metadata_.isValid = true;
    metadata_.filePath = videoFile.getFullPathName();
    metadata_.width = codecCtx_->width;
    metadata_.height = codecCtx_->height;

    // Calculate duration
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

    // Calculate frame rate
    AVRational fr = av_guess_frame_rate(formatCtx_, videoStream_, nullptr);
    if (fr.num > 0 && fr.den > 0)
    {
        metadata_.frameRate = av_q2d(fr);
    }
    else
    {
        metadata_.frameRate = 25.0; // fallback
    }

    const AVCodec* codec = codecCtx_->codec;
    metadata_.codecName = codec ? codec->name : "Unknown";
    metadata_.isHardwareAccelerated = (hwDeviceCtx_ != nullptr);
    metadata_.pixelFormatName = av_get_pix_fmt_name(codecCtx_->pix_fmt);

    // Prepare lock-free triple buffer
    frameQueue_.prepareBuffers(metadata_.width, metadata_.height);

    isHwAccelActive_.store(metadata_.isHardwareAccelerated, std::memory_order_release);
    isLoaded_.store(true, std::memory_order_release);

    // Ensure thread is running
    if (!isThreadRunning())
    {
        startThread(juce::Thread::Priority::high);
    }

    // Decode initial frame at 0.0s
    lastSeekTargetPts_.store(0.0, std::memory_order_release);
    seekTargetPts_.store(0.0, std::memory_order_release);
    seekRequested_.store(true, std::memory_order_release);

    notify();

    listeners_.call([&](Listener& l) { l.videoLoaded(metadata_); });
    return true;
}

void FFmpegDecoder::closeFile()
{
    isLoaded_.store(false, std::memory_order_release);
    lastSeekTargetPts_.store(-1.0, std::memory_order_release);
    frameQueue_.clear();
    cleanupFFmpeg();
    metadata_ = VideoMetadata();
}

bool FFmpegDecoder::initHardwareContext()
{
    // Try to create Apple VideoToolbox hardware device context
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    if (ret < 0)
    {
        hwDeviceCtx_ = nullptr;
        return false;
    }
    return true;
}

bool FFmpegDecoder::initCodecContext()
{
    const AVCodec* codec = avcodec_find_decoder(videoStream_->codecpar->codec_id);
    if (!codec)
    {
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_)
    {
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, videoStream_->codecpar) < 0)
    {
        return false;
    }

    // Configure hardware callback if VideoToolbox is available
    if (hwDeviceCtx_ != nullptr)
    {
        codecCtx_->get_format = get_hw_format_callback;
        codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    }

    // Enable multi-threaded decoding
    codecCtx_->thread_count = 0; // FFmpeg auto-selects optimal thread count

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0)
    {
        return false;
    }

    return true;
}

void FFmpegDecoder::cleanupFFmpeg()
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

void FFmpegDecoder::setPlaybackState(bool isPlaying, double targetPts)
{
    bool wasPlaying = isPlaying_.load(std::memory_order_relaxed);
    isPlaying_.store(isPlaying, std::memory_order_release);
    targetPts_.store(targetPts, std::memory_order_release);

    double currentPts = currentDecodedPts_.load(std::memory_order_relaxed);
    double delta = targetPts - currentPts;

    // If playback state just started, seek to starting point
    if (!wasPlaying && isPlaying)
    {
        lastSeekTargetPts_.store(targetPts, std::memory_order_relaxed);
        requestSeek(targetPts);
    }
    else if (!isPlaying)
    {
        // When stopped / paused: only seek if the target position has actually changed
        // (e.g. note selection in score, user scrubbed timeline, or transport relocated)
        double lastTarget = lastSeekTargetPts_.load(std::memory_order_relaxed);
        double frameDuration = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;
        double threshold = std::max(0.015, frameDuration * 0.4);

        if (lastTarget < 0.0 || std::abs(targetPts - lastTarget) >= threshold)
        {
            lastSeekTargetPts_.store(targetPts, std::memory_order_relaxed);
            requestSeek(targetPts);
        }
    }
    else
    {
        // Continuous playback: ONLY seek if user looped or jumped backwards, or jumped far ahead (> 1.5s)
        if (delta < -0.3 || delta > 1.5)
        {
            lastSeekTargetPts_.store(targetPts, std::memory_order_relaxed);
            requestSeek(targetPts);
        }
        else
        {
            notify();
        }
    }
}

void FFmpegDecoder::requestSeek(double targetPts)
{
    double target = std::max(0.0, targetPts);
    if (seekRequested_.load(std::memory_order_relaxed) && std::abs(target - seekTargetPts_.load(std::memory_order_relaxed)) < 0.02)
    {
        return;
    }
    seekTargetPts_.store(target, std::memory_order_release);
    seekRequested_.store(true, std::memory_order_release);
    notify();
}

const DecodedVideoFrame* FFmpegDecoder::getLatestFrame() noexcept
{
    return frameQueue_.getLatestFrame();
}

VideoMetadata FFmpegDecoder::getMetadata() const
{
    return metadata_;
}

bool FFmpegDecoder::isFileLoaded() const noexcept
{
    return isLoaded_.load(std::memory_order_acquire);
}

bool FFmpegDecoder::isHardwareAccelerated() const noexcept
{
    return isHwAccelActive_.load(std::memory_order_acquire);
}

double FFmpegDecoder::getCurrentDecodedPts() const noexcept
{
    return currentDecodedPts_.load(std::memory_order_acquire);
}

void FFmpegDecoder::run()
{
    while (!threadShouldExit())
    {
        if (!isLoaded_.load(std::memory_order_acquire))
        {
            wait(50);
            continue;
        }

        // Handle seek requests with top priority
        if (seekRequested_.load(std::memory_order_acquire))
        {
            double target = seekTargetPts_.load(std::memory_order_acquire);
            seekRequested_.store(false, std::memory_order_release);

            performSeek(target);
            continue;
        }

        bool playing = isPlaying_.load(std::memory_order_acquire);
        double target = targetPts_.load(std::memory_order_acquire);
        double current = currentDecodedPts_.load(std::memory_order_acquire);

        if (playing)
        {
            double frameDuration = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;
            // If we are behind target playhead, decode next frame
            if (current + (frameDuration * 0.5) < target)
            {
                bool eof = false;
                if (!decodeNextFrame(eof, -1.0))
                {
                    if (eof)
                    {
                        wait(10);
                    }
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

bool FFmpegDecoder::performSeek(double targetSeconds)
{
    std::lock_guard<std::mutex> lock(fileMutex_);

    if (!formatCtx_ || !codecCtx_ || videoStreamIndex_ < 0)
        return false;

    // Convert seconds to stream time base
    int64_t targetTimestamp = static_cast<int64_t>(targetSeconds / av_q2d(videoStream_->time_base));

    // Seek to keyframe backward
    int ret = av_seek_frame(formatCtx_, videoStreamIndex_, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        // Try general seek flag if backward fails
        ret = av_seek_frame(formatCtx_, videoStreamIndex_, targetTimestamp, 0);
    }

    avcodec_flush_buffers(codecCtx_);

    // Forward decode until we reach the target PTS
    bool reachedEof = false;
    double frameDuration = (metadata_.frameRate > 0.0) ? (1.0 / metadata_.frameRate) : 0.04;

    while (!reachedEof && !threadShouldExit())
    {
        if (seekRequested_.load(std::memory_order_relaxed))
            return false; // Newer seek supersedes this one

        if (!decodeNextFrame(reachedEof, targetSeconds))
        {
            if (reachedEof) break;
            continue;
        }

        double decodedPts = currentDecodedPts_.load(std::memory_order_relaxed);
        if (decodedPts >= targetSeconds - (frameDuration * 0.5))
        {
            break;
        }
    }

    return true;
}

bool FFmpegDecoder::decodeNextFrame(bool& reachedEof, double targetCommitPts)
{
    reachedEof = false;

    if (!formatCtx_ || !codecCtx_ || !packet_ || !hwFrame_ || !swFrame_)
        return false;

    while (!threadShouldExit())
    {
        int ret = avcodec_receive_frame(codecCtx_, hwFrame_);
        if (ret == 0)
        {
            // Got a valid decoded frame!
            AVFrame* finalFrame = hwFrame_;

            // If hardware pixel format (VideoToolbox), copy back to software frame memory
            if (hwFrame_->format == AV_PIX_FMT_VIDEOTOOLBOX)
            {
                if (av_hwframe_transfer_data(swFrame_, hwFrame_, 0) >= 0)
                {
                    swFrame_->pts = hwFrame_->pts;
                    swFrame_->pkt_dts = hwFrame_->pkt_dts;
                    finalFrame = swFrame_;
                }
            }

            // Calculate presentation timestamp
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
                // Write to lock-free frame queue
                DecodedVideoFrame* writeBuf = frameQueue_.getWriteBuffer();
                if (writeBuf)
                {
                    writeBuf->pts = ptsSeconds;
                    writeBuf->duration = frameDuration;
                    convertFrameToImage(finalFrame, writeBuf);
                    frameQueue_.commitWriteBuffer();
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
            // Read next video packet from demuxer
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
                // End of container: flush codec
                avcodec_send_packet(codecCtx_, nullptr);
                ret = avcodec_receive_frame(codecCtx_, hwFrame_);
                if (ret == AVERROR_EOF)
                {
                    reachedEof = true;
                    return false;
                }
                else if (ret < 0)
                {
                    return false;
                }
            }
        }
        else
        {
            return false;
        }
    }

    return false;
}

void FFmpegDecoder::convertFrameToImage(AVFrame* srcFrame, DecodedVideoFrame* destFrame)
{
    int width = srcFrame->width;
    int height = srcFrame->height;

    if (width <= 0 || height <= 0)
        return;

    // Ensure image dimensions match
    if (destFrame->image.getWidth() != width || destFrame->image.getHeight() != height)
    {
        destFrame->image = juce::Image(juce::Image::ARGB, width, height, true);
    }

    // Initialize or update SwsContext
    // JUCE ARGB format on macOS memory is in BGRA byte order (AV_PIX_FMT_BGRA)
    swsCtx_ = sws_getCachedContext(swsCtx_,
                                   width, height, static_cast<AVPixelFormat>(srcFrame->format),
                                   width, height, AV_PIX_FMT_BGRA,
                                   SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsCtx_)
        return;

    juce::Image::BitmapData destData(destFrame->image, juce::Image::BitmapData::writeOnly);

    uint8_t* destPointers[4] = { destData.data, nullptr, nullptr, nullptr };
    int destLineSizes[4] = { destData.lineStride, 0, 0, 0 };

    sws_scale(swsCtx_,
              srcFrame->data,
              srcFrame->linesize,
              0,
              height,
              destPointers,
              destLineSizes);
}

} // namespace MScoreVideoSync
