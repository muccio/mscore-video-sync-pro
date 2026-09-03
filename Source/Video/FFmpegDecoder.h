#pragma once

#include "../Core/CommonTypes.h"
#include "LockFreeFrameQueue.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <memory>
#include <mutex>

// Forward declarations for FFmpeg C structs
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct AVBufferRef;

namespace MScoreVideoSync
{

class FFmpegDecoder : public juce::Thread
{
public:
    FFmpegDecoder();
    ~FFmpegDecoder() override;

    // Open video file and initialize VideoToolbox / software decoders
    bool openFile(const juce::File& videoFile);
    void closeFile();

    // Transport synchronization updates (called from UI / Audio sync layer)
    void setPlaybackState(bool isPlaying, double targetPts);
    void requestSeek(double targetPts);

    // Lock-free access to latest decoded frame for rendering
    const DecodedVideoFrame* getLatestFrame() noexcept;

    // Metadata and state queries
    VideoMetadata getMetadata() const;
    bool isFileLoaded() const noexcept;
    bool isHardwareAccelerated() const noexcept;
    double getCurrentDecodedPts() const noexcept;

    // Listener for file load completion or errors
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void videoLoaded(const VideoMetadata& metadata) = 0;
        virtual void videoLoadFailed(const juce::String& errorMessage) = 0;
    };

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    // juce::Thread main loop
    void run() override;

    // FFmpeg initialization routines
    bool initHardwareContext();
    bool initCodecContext();
    void cleanupFFmpeg();

    // Frame decoding and conversion helpers
    bool decodeNextFrame(bool& reachedEof, double targetCommitPts = -1.0);
    bool performSeek(double targetSeconds);
    void convertFrameToImage(AVFrame* srcFrame, DecodedVideoFrame* destFrame);

    // FFmpeg context pointers
    AVFormatContext* formatCtx_{ nullptr };
    AVCodecContext* codecCtx_{ nullptr };
    AVBufferRef* hwDeviceCtx_{ nullptr };
    AVFrame* hwFrame_{ nullptr };
    AVFrame* swFrame_{ nullptr };
    AVPacket* packet_{ nullptr };
    SwsContext* swsCtx_{ nullptr };
    int videoStreamIndex_{ -1 };
    AVStream* videoStream_{ nullptr };

    // Video properties
    VideoMetadata metadata_;
    std::atomic<bool> isLoaded_{ false };
    std::atomic<bool> isHwAccelActive_{ false };
    std::atomic<double> currentDecodedPts_{ 0.0 };

    // Playback control and synchronization
    std::atomic<bool> isPlaying_{ false };
    std::atomic<double> targetPts_{ 0.0 };
    std::atomic<bool> seekRequested_{ false };
    std::atomic<double> seekTargetPts_{ 0.0 };
    std::atomic<double> lastSeekTargetPts_{ -1.0 };

    // Frame buffer
    LockFreeFrameQueue frameQueue_;

    // Thread synchronization & listeners
    std::mutex fileMutex_;
    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFmpegDecoder)
};

} // namespace MScoreVideoSync
