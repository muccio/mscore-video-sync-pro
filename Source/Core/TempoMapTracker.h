#pragma once

#include "CommonTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace MScoreVideoSync
{

class TempoMapTracker
{
public:
    TempoMapTracker() noexcept;
    ~TempoMapTracker() = default;

    // Real-time audio processing entry point
    template <typename FloatType>
    void processAudioBlock(const juce::AudioPlayHead* playHead,
                           const juce::AudioBuffer<FloatType>& buffer,
                           const juce::MidiBuffer& midiMessages,
                           double sampleRate) noexcept;

    // Legacy update method
    void updateFromPlayHead(const juce::AudioPlayHead::PositionInfo& posInfo, double sampleRate) noexcept;

    // Direct snapshot update
    void update(const TransportSnapshot& snapshot) noexcept;

    // Get current transport snapshot (lock-free, real-time safe)
    TransportSnapshot getSnapshot() const noexcept;

    // Periodic watchdog check called from UI timer (60Hz) to detect when host stops calling processBlock
    void checkWatchdog(double currentWallTimeSeconds) noexcept;

    // Video offset in seconds (-30.0s to +30.0s)
    void setVideoOffsetSeconds(double offsetSeconds) noexcept;
    double getVideoOffsetSeconds() const noexcept;

    // Sync Mode control
    void setSyncMode(SyncMode mode) noexcept;
    SyncMode getSyncMode() const noexcept;

    // Score Tempo (BPM) and Time Signature controls
    void setScoreTempoBpm(double bpm) noexcept;
    double getScoreTempoBpm() const noexcept;
    void setScoreTimeSignature(int numerator, int denominator) noexcept;
    int getTimeSigNumerator() const noexcept;
    int getTimeSigDenominator() const noexcept;
    void setTempoUserOverridden(bool overridden) noexcept;
    bool isTempoUserOverridden() const noexcept;

    // Manual controls
    void resetInternalClock() noexcept;
    void setManualTime(double timeSeconds) noexcept;
    void setManualPlaying(bool playing) noexcept;

    // Bar / Beat utilities
    static void calculateBarBeat(double ppq, int timeSigNum, int timeSigDenom,
                                 int& outBar, int& outBeat, double& outFraction) noexcept;
    static juce::String formatMeasureBeat(double ppq, int timeSigNum, int timeSigDenom, bool concise = true);

    // Calculate Presentation Timestamp (PTS)
    double calculateTargetPts(double hostSeconds) const noexcept;
    double getInterpolatedPts(double hostSeconds, double elapsedSecondsSinceBlock, bool isPlaying) const noexcept;

private:
    // Core transport atomics
    std::atomic<bool> isPlaying_{ false };
    std::atomic<bool> isRecording_{ false };
    std::atomic<bool> isLooping_{ false };
    std::atomic<double> timeInSeconds_{ 0.0 };
    std::atomic<int64_t> timeInSamples_{ 0 };
    std::atomic<double> bpm_{ 120.0 };
    std::atomic<bool> isTempoUserOverridden_{ false };
    std::atomic<double> ppqPosition_{ 0.0 };
    std::atomic<int> timeSigNumerator_{ 4 };
    std::atomic<int> timeSigDenominator_{ 4 };
    std::atomic<double> sampleRate_{ 44100.0 };
    std::atomic<double> videoOffsetSeconds_{ 0.0 };

    // Sync mode & telemetry
    std::atomic<SyncMode> syncMode_{ SyncMode::DAWPlayHead };
    std::atomic<int> activeSource_{ 0 }; // 0: Stopped, 1: Host Samples/DAW, 2: Audio Stream, 3: MIDI, 4: Manual
    std::atomic<float> audioLevel_{ 0.0f };
    std::atomic<bool> midiActive_{ false };

    // Audio thread state (strictly accessed in processAudioBlock)
    double accumulatedSampleTimeSec_{ 0.0 };
    int64_t lastSamplePosition_{ -1 };
    double lastHostTimeSec_{ -1.0 };
    double lastBlockWallTime_{ 0.0 };
    int activeFramesRemaining_{ 0 };

    // MIDI tracking state
    double midiClockPpq_{ 0.0 };
    bool midiPlaying_{ false };
};

} // namespace MScoreVideoSync
