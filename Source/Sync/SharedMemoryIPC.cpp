#include "SharedMemoryIPC.h"

namespace MScoreVideoSync
{

SharedMemoryIPC::SharedMemoryIPC()
{
}

SharedMemoryIPC::~SharedMemoryIPC()
{
    close();
}

void SharedMemoryIPC::close()
{
    if (header_ != nullptr)
    {
        munmap(header_, sizeof(MScoreVideoSyncIPCHeader));
        header_ = nullptr;
    }

    if (shmFd_ >= 0)
    {
        ::close(shmFd_);
        shmFd_ = -1;
    }

    if (mode_ == Mode::Producer && shmName_.isNotEmpty())
    {
        shm_unlink(shmName_.toRawUTF8());
    }

    mode_ = Mode::None;
}

bool SharedMemoryIPC::startProducer(const char* shmName)
{
    close();
    shmName_ = shmName;

    // Unlink any stale segment first
    shm_unlink(shmName_.toRawUTF8());

    shmFd_ = shm_open(shmName_.toRawUTF8(), O_CREAT | O_RDWR, 0666);
    if (shmFd_ < 0)
    {
        return false;
    }

    if (ftruncate(shmFd_, sizeof(MScoreVideoSyncIPCHeader)) < 0)
    {
        ::close(shmFd_);
        shmFd_ = -1;
        shm_unlink(shmName_.toRawUTF8());
        return false;
    }

    void* addr = mmap(nullptr, sizeof(MScoreVideoSyncIPCHeader),
                      PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);

    if (addr == MAP_FAILED)
    {
        ::close(shmFd_);
        shmFd_ = -1;
        shm_unlink(shmName_.toRawUTF8());
        return false;
    }

    header_ = new (addr) MScoreVideoSyncIPCHeader();
    header_->magic = IPC_MAGIC_HEADER;
    header_->version = IPC_CURRENT_VERSION;
    header_->sequenceCounter.store(1, std::memory_order_release);

    mode_ = Mode::Producer;
    return true;
}

bool SharedMemoryIPC::startConsumer(const char* shmName)
{
    close();
    shmName_ = shmName;

    shmFd_ = shm_open(shmName_.toRawUTF8(), O_RDONLY, 0666);
    if (shmFd_ < 0)
    {
        return false;
    }

    void* addr = mmap(nullptr, sizeof(MScoreVideoSyncIPCHeader),
                      PROT_READ, MAP_SHARED, shmFd_, 0);

    if (addr == MAP_FAILED)
    {
        ::close(shmFd_);
        shmFd_ = -1;
        return false;
    }

    auto* candidate = reinterpret_cast<MScoreVideoSyncIPCHeader*>(addr);
    if (candidate->magic != IPC_MAGIC_HEADER || candidate->version != IPC_CURRENT_VERSION)
    {
        munmap(addr, sizeof(MScoreVideoSyncIPCHeader));
        ::close(shmFd_);
        shmFd_ = -1;
        return false;
    }

    header_ = candidate;
    mode_ = Mode::Consumer;
    lastReadSequence_ = 0;
    return true;
}

void SharedMemoryIPC::writeProducerState(const TransportSnapshot& snapshot,
                                         const juce::String& videoFilePath,
                                         SMPTEFrameRate smpteRate,
                                         SyncMode syncMode) noexcept
{
    if (header_ == nullptr || mode_ != Mode::Producer)
        return;

    // Direct atomic updates (Cache-Line Aligned, Zero-Alloc, Zero-Lock)
    header_->isPlaying.store(snapshot.isPlaying, std::memory_order_relaxed);
    header_->isLooping.store(snapshot.isLooping, std::memory_order_relaxed);
    header_->sampleRate.store(snapshot.sampleRate, std::memory_order_relaxed);
    header_->timeInSamples.store(snapshot.timeInSamples, std::memory_order_relaxed);
    header_->timeInSeconds.store(snapshot.timeInSeconds, std::memory_order_relaxed);
    header_->ppqPosition.store(snapshot.ppqPosition, std::memory_order_relaxed);
    header_->bpm.store(snapshot.bpm, std::memory_order_relaxed);
    header_->timeSigNumerator.store(snapshot.timeSigNumerator, std::memory_order_relaxed);
    header_->timeSigDenominator.store(snapshot.timeSigDenominator, std::memory_order_relaxed);
    header_->barCount.store(snapshot.bar, std::memory_order_relaxed);
    header_->videoOffsetSeconds.store(snapshot.videoOffsetSeconds + snapshot.movieStartOffsetSeconds + snapshot.displayLatencySeconds, std::memory_order_relaxed);
    header_->smpteRate.store(static_cast<int32_t>(smpteRate), std::memory_order_relaxed);
    header_->syncMode.store(static_cast<int32_t>(syncMode), std::memory_order_relaxed);

    int64_t nowNs = juce::Time::getHighResolutionTicks();
    header_->hostTimeNs.store(nowNs, std::memory_order_relaxed);

    // Copy path safely if changed
    if (videoFilePath.isNotEmpty())
    {
        const char* rawPath = videoFilePath.toRawUTF8();
        std::strncpy(header_->videoFilePath, rawPath, sizeof(header_->videoFilePath) - 1);
        header_->videoFilePath[sizeof(header_->videoFilePath) - 1] = '\0';
    }

    // Sequence counter increment marks atomic release
    header_->sequenceCounter.fetch_add(1, std::memory_order_release);
}

bool SharedMemoryIPC::readConsumerState(TransportSnapshot& outSnapshot,
                                        juce::String& outVideoFilePath,
                                        SMPTEFrameRate& outSmpteRate,
                                        SyncMode& outSyncMode) noexcept
{
    if (header_ == nullptr || mode_ != Mode::Consumer)
        return false;

    uint64_t seq = header_->sequenceCounter.load(std::memory_order_acquire);
    if (seq == 0)
        return false;

    outSnapshot.isPlaying = header_->isPlaying.load(std::memory_order_relaxed);
    outSnapshot.isLooping = header_->isLooping.load(std::memory_order_relaxed);
    outSnapshot.sampleRate = header_->sampleRate.load(std::memory_order_relaxed);
    outSnapshot.timeInSamples = header_->timeInSamples.load(std::memory_order_relaxed);
    outSnapshot.timeInSeconds = header_->timeInSeconds.load(std::memory_order_relaxed);
    outSnapshot.ppqPosition = header_->ppqPosition.load(std::memory_order_relaxed);
    outSnapshot.bpm = header_->bpm.load(std::memory_order_relaxed);
    outSnapshot.timeSigNumerator = header_->timeSigNumerator.load(std::memory_order_relaxed);
    outSnapshot.timeSigDenominator = header_->timeSigDenominator.load(std::memory_order_relaxed);
    outSnapshot.bar = header_->barCount.load(std::memory_order_relaxed);
    outSnapshot.videoOffsetSeconds = header_->videoOffsetSeconds.load(std::memory_order_relaxed);
    outSnapshot.isLockedToHost = true;
    outSnapshot.activeSource = "Shared Memory IPC (MuseScore)";

    outSmpteRate = static_cast<SMPTEFrameRate>(header_->smpteRate.load(std::memory_order_relaxed));
    outSyncMode = static_cast<SyncMode>(header_->syncMode.load(std::memory_order_relaxed));

    char pathBuf[512] = { 0 };
    std::memcpy(pathBuf, header_->videoFilePath, sizeof(pathBuf) - 1);
    outVideoFilePath = juce::String::fromUTF8(pathBuf);

    lastReadSequence_ = seq;
    return true;
}

} // namespace MScoreVideoSync
