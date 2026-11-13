#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <map>
#include <mutex>
#include <atomic>

namespace MScoreVideoSync
{

class FilmstripGenerator : public juce::Thread
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void filmstripUpdated() = 0;
    };

    FilmstripGenerator();
    ~FilmstripGenerator() override;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    void startExtraction(const juce::File& videoFile, double durationSeconds, double intervalSeconds = 2.0);
    void cancel();

    // Fast lookup for timeline rendering
    juce::Image getThumbnailForTime(double timeSeconds, double intervalSeconds = 2.0) const;
    double getIntervalSeconds() const noexcept { return intervalSeconds_; }
    bool isComplete() const noexcept { return isComplete_.load(std::memory_order_relaxed); }
    float getProgress() const noexcept { return progress_.load(std::memory_order_relaxed); }

private:
    void run() override;

    juce::File currentFile_;
    double durationSeconds_{ 0.0 };
    double intervalSeconds_{ 2.0 };

    mutable std::mutex cacheMutex_;
    std::map<int, juce::Image> thumbnailCache_; // key = intervalIndex (e.g. time / interval)

    std::atomic<bool> isComplete_{ false };
    std::atomic<float> progress_{ 0.0f };

    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilmstripGenerator)
};

} // namespace MScoreVideoSync
