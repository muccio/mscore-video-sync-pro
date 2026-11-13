#pragma once

#include "../Core/CommonTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace MScoreVideoSync
{

class TransportSyncEngine
{
public:
    TransportSyncEngine() noexcept;
    ~TransportSyncEngine() = default;

    // REAL-TIME AUDIO SAFE: Entry point called every processBlock
    template <typename FloatType>
    void processAudioBlock(const juce::AudioPlayHead* playHead,
                           const juce::AudioBuffer<FloatType>& buffer,
                           const juce::MidiBuffer& midiMessages,
                           double sampleRate) noexcept;

    // Lock-free snapshot retrieval for UI and video engine
    TransportSnapshot getSnapshot() const noexcept;

    // Watchdog called from UI 60Hz timer to detect when host stops calling processBlock.
    // Returns true when a Playing -> Stopped timeout transition occurred.
    bool checkWatchdog(double currentWallTimeSeconds) noexcept;

    // Smart Handoff & Free Scrub Controls
    void freeScrubToTime(double timeSeconds) noexcept;
    void stepFrames(int frameDelta, SMPTEFrameRate rate) noexcept;
    void stepSeconds(double secondsDelta) noexcept;
    void setLocalPlaying(bool playing) noexcept;
    bool isLocalPlaying() const noexcept { return isLocalPlaying_.load(std::memory_order_relaxed); }
    void forceReLockToHost() noexcept;

    // Offset & Calibration Controls
    void setVideoOffsetSeconds(double offsetSeconds) noexcept;
    double getVideoOffsetSeconds() const noexcept;

    void setSmpteStartTime(double smpteStartSeconds) noexcept;
    double getSmpteStartTime() const noexcept;

    void setMovieStartOffset(double movieOffsetSeconds) noexcept;
    double getMovieStartOffset() const noexcept;

    void setDisplayLatencyCompensation(double latencySeconds) noexcept;
    double getDisplayLatencyCompensation() const noexcept;

    void setTrimRange(double trimInSeconds, double trimOutSeconds) noexcept;
    double getTrimInSeconds() const noexcept;
    double getTrimOutSeconds() const noexcept;

    // Sync Mode
    void setSyncMode(SyncMode mode) noexcept;
    SyncMode getSyncMode() const noexcept;

    // Manual Tempo / Metric Overrides
    void setScoreTempoBpm(double bpm) noexcept;
    double getScoreTempoBpm() const noexcept;
    void setScoreTimeSignature(int numerator, int denominator) noexcept;
    int getTimeSigNumerator() const noexcept;
    int getTimeSigDenominator() const noexcept;

    // Metric calculation utilities
    static void calculateBarBeat(double ppq, int timeSigNum, int timeSigDenom,
                                 int& outBar, double& outBeat) noexcept;
    static juce::String formatMeasureBeat(int bar, double beat, int timeSigNum, int timeSigDenom);

private:
    // Core transport atomics
    std::atomic<bool> isPlaying_{ false };
    std::atomic<bool> isRecording_{ false };
    std::atomic<bool> isLooping_{ false };
    std::atomic<double> timeInSeconds_{ 0.0 };
    std::atomic<int64_t> timeInSamples_{ 0 };
    std::atomic<double> bpm_{ 120.0 };
    std::atomic<double> ppqPosition_{ 0.0 };
    std::atomic<int> timeSigNumerator_{ 4 };
    std::atomic<int> timeSigDenominator_{ 4 };
    std::atomic<int> bar_{ 1 };
    std::atomic<double> beat_{ 1.0 };
    std::atomic<double> sampleRate_{ 44100.0 };

    // Offsets & Calibrations
    std::atomic<double> videoOffsetSeconds_{ 0.0 };
    std::atomic<double> smpteStartTimeSeconds_{ 0.0 };
    std::atomic<double> movieStartOffsetSeconds_{ 0.0 };
    std::atomic<double> displayLatencySeconds_{ 0.0 };
    std::atomic<double> trimInSeconds_{ 0.0 };
    std::atomic<double> trimOutSeconds_{ 0.0 };

    // Smart Handoff states
    std::atomic<SyncMode> syncMode_{ SyncMode::HostLockAutoScrub };
    std::atomic<bool> isFreeScrubActive_{ false };
    std::atomic<double> freeScrubTime_{ 0.0 };
    std::atomic<bool> isLocalPlaying_{ false };
    std::atomic<double> localPlayStartWallTime_{ 0.0 };
    std::atomic<double> localPlayStartPts_{ 0.0 };
    std::atomic<bool> isLockedToHost_{ true };

    // Audio thread tracking state
    int64_t lastHostSample_{ -1 };
    double lastHostTimeSec_{ -1.0 };
    std::atomic<double> lastBlockWallTime_{ 0.0 };
    double accumulatedSampleTimeSec_{ 0.0 };

    // Quirks detection
    double lastObservedHostSec_{ -1.0 };
};

} // namespace MScoreVideoSync
