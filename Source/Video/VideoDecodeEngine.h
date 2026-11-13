#pragma once

#include "../Core/CommonTypes.h"
#include "LockFreeFrameQueue.h"
#include "FilmstripGenerator.h"
#include "WaveformExtractor.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <deque>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct AVBufferRef;

namespace MScoreVideoSync
{

class VideoDecodeEngine : public juce::Thread
{
public:
    VideoDecodeEngine();
    ~VideoDecodeEngine() override;

    bool openFile(const juce::File& videoFile);
    void closeFile();

    // Transport synchronization updates
    void setPlaybackState(bool isPlaying, double targetPts);
    void requestSeek(double targetPts);

    // Frame retrieval for rendering (lock-free)
    const DecodedVideoFrame* getLatestFrame() noexcept;

    // Metadata & status
    VideoMetadata getMetadata() const;
    bool isFileLoaded() const noexcept;
    bool isHardwareAccelerated() const noexcept;
    double getCurrentDecodedPts() const noexcept;

    // Background track extractors
    FilmstripGenerator& getFilmstripGenerator() noexcept { return filmstripGenerator_; }
    WaveformExtractor& getWaveformExtractor() noexcept { return waveformExtractor_; }

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
    void run() override;

    bool initHardwareContext();
    bool initCodecContext();
    void cleanupFFmpeg();

    bool decodeNextFrame(bool& reachedEof, double targetCommitPts = -1.0);
    bool performSeek(double targetSeconds);
    void convertFrameToImage(AVFrame* srcFrame, DecodedVideoFrame* destFrame);

    // Predictive Ring Buffer for bidirectional scrubbing
    struct CachedFrame
    {
        double pts = 0.0;
        juce::Image image;
    };

    bool tryLookupCachedFrame(double targetPts, DecodedVideoFrame* dest);
    void cacheFrame(double pts, const juce::Image& image);

    AVFormatContext* formatCtx_{ nullptr };
    AVCodecContext* codecCtx_{ nullptr };
    AVBufferRef* hwDeviceCtx_{ nullptr };
    AVFrame* hwFrame_{ nullptr };
    AVFrame* swFrame_{ nullptr };
    AVPacket* packet_{ nullptr };
    SwsContext* swsCtx_{ nullptr };
    int videoStreamIndex_{ -1 };
    AVStream* videoStream_{ nullptr };

    VideoMetadata metadata_;
    std::atomic<bool> isLoaded_{ false };
    std::atomic<bool> isHwAccelActive_{ false };
    std::atomic<double> currentDecodedPts_{ 0.0 };

    std::atomic<bool> isPlaying_{ false };
    std::atomic<double> targetPts_{ 0.0 };
    std::atomic<double> prevPlaybackTargetPts_{ -1.0 };
    std::atomic<bool> seekRequested_{ false };
    std::atomic<bool> isSeeking_{ false };
    std::atomic<bool> streamNeedsSeek_{ false };
    std::atomic<double> seekTargetPts_{ 0.0 };
    std::atomic<double> lastSeekTargetPts_{ -1.0 };

    LockFreeFrameQueue frameQueue_;

    // Bidirectional scrubbing cache (holds up to 32 frames around playhead)
    static constexpr size_t MaxCachedFrames = 32;
    std::deque<CachedFrame> frameRingCache_;
    std::mutex cacheMutex_;

    // Sub-generators
    FilmstripGenerator filmstripGenerator_;
    WaveformExtractor waveformExtractor_;

    std::mutex fileMutex_;
    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoDecodeEngine)
};

} // namespace MScoreVideoSync
