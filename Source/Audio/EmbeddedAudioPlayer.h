#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <mutex>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace MScoreVideoSync
{

class EmbeddedAudioPlayer : public juce::Thread
{
public:
    EmbeddedAudioPlayer();
    ~EmbeddedAudioPlayer() override;

    bool openFile(const juce::File& videoFile, double targetSampleRate);
    void closeFile();

    void prepareToPlay(double sampleRate) noexcept;
    void resetFifo() noexcept;

    // Transport synchronization updates
    void setPlaybackState(bool isPlaying, double targetPts);
    void requestSeek(double targetPts);

    // Audio mixing and level controls (REAL-TIME SAFE)
    template <typename FloatType>
    void processBlockAdding(juce::AudioBuffer<FloatType>& buffer, int numSamples) noexcept;

    // Volume, Mute & Dim Controls
    void setGainDecibels(float gainDb) noexcept;
    float getGainDecibels() const noexcept { return gainDb_.load(std::memory_order_relaxed); }

    void setMuted(bool muted) noexcept;
    bool isMuted() const noexcept { return isMuted_.load(std::memory_order_relaxed); }

    void setDimmed(bool dimmed) noexcept;
    bool isDimmed() const noexcept { return isDimmed_.load(std::memory_order_relaxed); }

    // Metering
    float getPeakLeft() const noexcept { return peakLeft_.load(std::memory_order_relaxed); }
    float getPeakRight() const noexcept { return peakRight_.load(std::memory_order_relaxed); }

    bool hasAudioStream() const noexcept { return audioStreamIndex_ >= 0; }

private:
    void run() override;
    void cleanupFFmpeg();
    bool initResamplerLocked(double targetRate);
    bool decodeAudioPacket(bool& reachedEof);

    AVFormatContext* formatCtx_{ nullptr };
    AVCodecContext* codecCtx_{ nullptr };
    SwrContext* swrCtx_{ nullptr };
    AVFrame* frame_{ nullptr };
    AVPacket* packet_{ nullptr };
    int audioStreamIndex_{ -1 };
    AVStream* audioStream_{ nullptr };

    double hostSampleRate_{ 44100.0 };
    std::atomic<double> pendingSampleRate_{ 44100.0 };
    std::atomic<bool> needsResamplerReinit_{ false };
    std::atomic<bool> isLoaded_{ false };
    std::atomic<bool> isPlaying_{ false };
    std::atomic<double> targetPts_{ 0.0 };
    std::atomic<double> prevPlaybackTargetPts_{ -1.0 };
    std::atomic<bool> seekRequested_{ false };
    std::atomic<double> seekTargetPts_{ 0.0 };

    // Gain / Mute / Dim / Metering
    std::atomic<float> gainDb_{ 0.0f };
    std::atomic<float> linearGain_{ 1.0f };
    std::atomic<bool> isMuted_{ false };
    std::atomic<bool> isDimmed_{ false };
    std::atomic<float> peakLeft_{ 0.0f };
    std::atomic<float> peakRight_{ 0.0f };

    // Lock-free circular FIFO for resampled stereo audio
    static constexpr int FifoCapacity = 65536;
    juce::AbstractFifo fifo_{ FifoCapacity };
    juce::AudioBuffer<float> fifoBuffer_{ 2, FifoCapacity };

    std::mutex fileMutex_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmbeddedAudioPlayer)
};

} // namespace MScoreVideoSync
