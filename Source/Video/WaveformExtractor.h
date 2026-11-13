#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <mutex>
#include <atomic>

namespace MScoreVideoSync
{

class WaveformExtractor : public juce::Thread
{
public:
    struct PeakPoint
    {
        float minVal = 0.0f;
        float maxVal = 0.0f;
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void waveformReady() = 0;
    };

    WaveformExtractor();
    ~WaveformExtractor() override;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    void startExtraction(const juce::File& videoFile, double durationSeconds, double timeResolution = 0.05); // 50ms buckets
    void cancel();

    std::vector<PeakPoint> getPeaksSnapshot() const;
    double getTimeResolution() const noexcept { return timeResolution_; }
    double getDurationSeconds() const noexcept { return durationSeconds_; }
    bool isComplete() const noexcept { return isComplete_.load(std::memory_order_relaxed); }

private:
    void run() override;

    juce::File currentFile_;
    double durationSeconds_{ 0.0 };
    double timeResolution_{ 0.05 };

    mutable std::mutex peakMutex_;
    std::vector<PeakPoint> peaks_;

    std::atomic<bool> isComplete_{ false };
    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformExtractor)
};

} // namespace MScoreVideoSync
