#include "TwoPopGenerator.h"

namespace MScoreVideoSync
{

TwoPopGenerator::TwoPopGenerator() noexcept
{
}

void TwoPopGenerator::prepare(double sampleRate) noexcept
{
    sampleRate_.store(sampleRate > 0.0 ? sampleRate : 44100.0, std::memory_order_release);
    phaseIncrement_ = (2.0 * juce::MathConstants<double>::pi * 1000.0) / sampleRate_.load(std::memory_order_relaxed);
    reset();
}

void TwoPopGenerator::reset() noexcept
{
    phase_ = 0.0;
    samplesRemaining_.store(0, std::memory_order_release);
    totalBurstSamples_.store(0, std::memory_order_release);
    isTriggered_.store(false, std::memory_order_release);
}

void TwoPopGenerator::trigger(double durationSeconds) noexcept
{
    double sr = sampleRate_.load(std::memory_order_relaxed);
    int numSamples = static_cast<int>(std::round(durationSeconds * sr));
    if (numSamples < 64) numSamples = 64;

    totalBurstSamples_.store(numSamples, std::memory_order_release);
    samplesRemaining_.store(numSamples, std::memory_order_release);
    isTriggered_.store(true, std::memory_order_release);
}

template <typename FloatType>
void TwoPopGenerator::processAdding(juce::AudioBuffer<FloatType>& buffer, int numSamples) noexcept
{
    int remaining = samplesRemaining_.load(std::memory_order_relaxed);
    if (remaining <= 0) return;

    if (isTriggered_.exchange(false, std::memory_order_acq_rel))
    {
        phase_ = 0.0;
    }

    int totalBurst = totalBurstSamples_.load(std::memory_order_relaxed);
    int fadeSamples = static_cast<int>(sampleRate_.load(std::memory_order_relaxed) * 0.005); // 5ms fade
    if (fadeSamples * 2 > totalBurst) fadeSamples = totalBurst / 4;

    int samplesToProcess = std::min(numSamples, remaining);
    int numChannels = buffer.getNumChannels();

    for (int i = 0; i < samplesToProcess; ++i)
    {
        int sampleIndexInBurst = totalBurst - remaining + i;

        // Cosine anti-click envelope
        float env = 1.0f;
        if (sampleIndexInBurst < fadeSamples && fadeSamples > 0)
        {
            env = 0.5f * (1.0f - std::cos(static_cast<float>(sampleIndexInBurst) / static_cast<float>(fadeSamples) * juce::MathConstants<float>::pi));
        }
        else if (sampleIndexInBurst > (totalBurst - fadeSamples) && fadeSamples > 0)
        {
            int tailIndex = totalBurst - sampleIndexInBurst;
            env = 0.5f * (1.0f - std::cos(static_cast<float>(tailIndex) / static_cast<float>(fadeSamples) * juce::MathConstants<float>::pi));
        }

        float sampleVal = std::sin(static_cast<float>(phase_)) * 0.6f * env;
        phase_ += phaseIncrement_;
        if (phase_ >= 2.0 * juce::MathConstants<double>::pi)
        {
            phase_ -= 2.0 * juce::MathConstants<double>::pi;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.addSample(ch, i, static_cast<FloatType>(sampleVal));
        }
    }

    samplesRemaining_.fetch_sub(samplesToProcess, std::memory_order_release);
}

template void TwoPopGenerator::processAdding<float>(juce::AudioBuffer<float>&, int) noexcept;
template void TwoPopGenerator::processAdding<double>(juce::AudioBuffer<double>&, int) noexcept;

} // namespace MScoreVideoSync
