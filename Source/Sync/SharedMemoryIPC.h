#pragma once

#include "../Core/CommonTypes.h"
#include <juce_core/juce_core.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <atomic>

namespace MScoreVideoSync
{

class SharedMemoryIPC
{
public:
    enum class Mode
    {
        None,
        Producer, // Plugin writing transport state
        Consumer  // Standalone/Satellite reading transport state
    };

    SharedMemoryIPC();
    ~SharedMemoryIPC();

    // Start as Producer (Plugin in MuseScore 4)
    bool startProducer(const char* shmName = IPC_SHARED_MEMORY_NAME);

    // Start as Consumer (Standalone Video Pro)
    bool startConsumer(const char* shmName = IPC_SHARED_MEMORY_NAME);

    void close();

    bool isConnected() const noexcept { return header_ != nullptr; }
    Mode getMode() const noexcept { return mode_; }

    // REAL-TIME AUDIO SAFE: Zero heap allocation, zero mutex locks
    void writeProducerState(const TransportSnapshot& snapshot,
                            const juce::String& videoFilePath,
                            SMPTEFrameRate smpteRate,
                            SyncMode syncMode) noexcept;

    // Called on consumer thread (60/120Hz)
    bool readConsumerState(TransportSnapshot& outSnapshot,
                           juce::String& outVideoFilePath,
                           SMPTEFrameRate& outSmpteRate,
                           SyncMode& outSyncMode) noexcept;

private:
    Mode mode_{ Mode::None };
    int shmFd_{ -1 };
    MScoreVideoSyncIPCHeader* header_{ nullptr };
    juce::String shmName_;
    uint64_t lastReadSequence_{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharedMemoryIPC)
};

} // namespace MScoreVideoSync
