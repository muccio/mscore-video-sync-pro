#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace MScoreVideoSync
{

class TwoPopGenerator
{
public:
    TwoPopGenerator() noexcept;
    ~TwoPopGenerator() = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Trigger 1-frame 1000 Hz beep
    void trigger(double durationSeconds = 0.041666) noexcept;

    // REAL-TIME AUDIO SAFE: Mixes into audio buffer with zero alloc
    template <typename FloatType>
    void processAdding(juce::AudioBuffer<FloatType>& buffer, int numSamples) noexcept;

    bool isPlaying() const noexcept { return samplesRemaining_.load(std::memory_order_relaxed) > 0; }

private:
    std::atomic<double> sampleRate_{ 44100.0 };
    std::atomic<int> samplesRemaining_{ 0 };
    std::atomic<int> totalBurstSamples_{ 0 };
    std::atomic<bool> isTriggered_{ false };

    double phase_{ 0.0 };
    double phaseIncrement_{ 0.0 };
};

} // namespace MScoreVideoSync
