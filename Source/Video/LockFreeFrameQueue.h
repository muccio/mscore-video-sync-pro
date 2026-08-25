#pragma once

#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <array>
#include <memory>
#include <mutex>

namespace MScoreVideoSync
{

struct DecodedVideoFrame
{
    juce::Image image;
    double pts = 0.0;
    int64_t frameNumber = 0;
    bool isKeyFrame = false;
    double duration = 0.0;
    bool isValid = false;

    DecodedVideoFrame() = default;

    DecodedVideoFrame(int width, int height)
        : image(juce::Image::ARGB, std::max(1, width), std::max(1, height), true)
        , pts(0.0)
        , frameNumber(0)
        , isKeyFrame(false)
        , duration(0.0)
        , isValid(false)
    {
    }
};

/**
 * Lock-free Triple Buffer for zero-latency frame exchange between Decoder Thread and UI Thread.
 * Allows the Producer (FFmpegDecoder) to continuously write frames without blocking the Consumer (UI).
 * Allows the Consumer (UI) to fetch the newest complete frame without holding mutexes.
 */
class LockFreeFrameQueue
{
public:
    static constexpr size_t BufferCount = 3;

    LockFreeFrameQueue()
    {
        for (size_t i = 0; i < BufferCount; ++i)
        {
            buffers_[i] = std::make_unique<DecodedVideoFrame>(1920, 1080);
        }
        writeIndex_.store(0, std::memory_order_relaxed);
        latestReadyIndex_.store(-1, std::memory_order_relaxed);
        readIndex_.store(1, std::memory_order_relaxed);
    }

    ~LockFreeFrameQueue() = default;

    // Resizes internal frame buffers if video dimensions change
    void prepareBuffers(int width, int height)
    {
        std::lock_guard<std::mutex> lock(resizeMutex_);
        for (size_t i = 0; i < BufferCount; ++i)
        {
            if (buffers_[i]->image.getWidth() != width || buffers_[i]->image.getHeight() != height)
            {
                buffers_[i]->image = juce::Image(juce::Image::ARGB, std::max(1, width), std::max(1, height), true);
                buffers_[i]->isValid = false;
            }
        }
    }

    // Called by Producer (FFmpegDecoder Thread) to get writable buffer
    DecodedVideoFrame* getWriteBuffer() noexcept
    {
        int writeIdx = writeIndex_.load(std::memory_order_relaxed);
        return buffers_[static_cast<size_t>(writeIdx)].get();
    }

    // Called by Producer after writing a new frame into write buffer
    void commitWriteBuffer() noexcept
    {
        int currentWrite = writeIndex_.load(std::memory_order_relaxed);
        int currentRead = readIndex_.load(std::memory_order_relaxed);

        // Mark current write buffer as ready
        buffers_[static_cast<size_t>(currentWrite)]->isValid = true;
        latestReadyIndex_.store(currentWrite, std::memory_order_release);

        // Find the slot that is neither latestReady nor currently being read
        for (int i = 0; i < static_cast<int>(BufferCount); ++i)
        {
            if (i != currentWrite && i != currentRead)
            {
                writeIndex_.store(i, std::memory_order_relaxed);
                break;
            }
        }
    }

    // Called by Consumer (UI Thread) to get the most recent frame
    // Returns nullptr if no new or valid frame is available
    const DecodedVideoFrame* getLatestFrame() noexcept
    {
        int latest = latestReadyIndex_.load(std::memory_order_acquire);
        if (latest < 0 || latest >= static_cast<int>(BufferCount))
            return nullptr;

        // Update readIndex so producer avoids overwriting what we might be drawing
        readIndex_.store(latest, std::memory_order_relaxed);

        if (buffers_[static_cast<size_t>(latest)]->isValid)
        {
            return buffers_[static_cast<size_t>(latest)].get();
        }
        return nullptr;
    }

    void clear() noexcept
    {
        latestReadyIndex_.store(-1, std::memory_order_release);
        for (size_t i = 0; i < BufferCount; ++i)
        {
            if (buffers_[i])
                buffers_[i]->isValid = false;
        }
    }

private:
    std::array<std::unique_ptr<DecodedVideoFrame>, BufferCount> buffers_;
    std::atomic<int> writeIndex_{ 0 };
    std::atomic<int> latestReadyIndex_{ -1 };
    std::atomic<int> readIndex_{ 1 };
    std::mutex resizeMutex_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockFreeFrameQueue)
};

} // namespace MScoreVideoSync
