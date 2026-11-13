#include "TransportSyncEngine.h"

namespace MScoreVideoSync
{

TransportSyncEngine::TransportSyncEngine() noexcept
{
}

template <typename FloatType>
void TransportSyncEngine::processAudioBlock(const juce::AudioPlayHead* playHead,
                                            const juce::AudioBuffer<FloatType>& buffer,
                                            const juce::MidiBuffer& /*midiMessages*/,
                                            double currentSampleRate) noexcept
{
    sampleRate_.store(currentSampleRate, std::memory_order_relaxed);
    double nowWallTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    lastBlockWallTime_.store(nowWallTime, std::memory_order_relaxed);

    if (playHead != nullptr)
    {
        auto posOpt = playHead->getPosition();
        if (posOpt.hasValue())
        {
            const auto& pos = *posOpt;

            bool hostIsPlaying = pos.getIsPlaying();
            bool hostIsLooping = pos.getIsLooping();
            bool hostIsRecording = pos.getIsRecording();

            auto timeInSamplesOpt = pos.getTimeInSamples();
            auto timeInSecondsOpt = pos.getTimeInSeconds();
            auto ppqOpt = pos.getPpqPosition();
            auto bpmOpt = pos.getBpm();
            auto timeSigOpt = pos.getTimeSignature();

            double currentBpm = bpmOpt.hasValue() ? *bpmOpt : 120.0;
            if (currentBpm <= 0.0) currentBpm = 120.0;
            bpm_.store(currentBpm, std::memory_order_relaxed);

            int num = 4, den = 4;
            if (timeSigOpt.hasValue())
            {
                num = timeSigOpt->numerator;
                den = timeSigOpt->denominator;
            }
            timeSigNumerator_.store(num, std::memory_order_relaxed);
            timeSigDenominator_.store(den, std::memory_order_relaxed);

            int64_t samples = timeInSamplesOpt.hasValue() ? *timeInSamplesOpt : 0;
            double seconds = timeInSecondsOpt.hasValue() ? *timeInSecondsOpt : 0.0;
            if (!timeInSecondsOpt.hasValue() && currentSampleRate > 0.0)
            {
                seconds = static_cast<double>(samples) / currentSampleRate;
            }

            double ppq = ppqOpt.hasValue() ? *ppqOpt : (seconds * currentBpm / 60.0);
            ppqPosition_.store(ppq, std::memory_order_relaxed);

            int currentBar = 1;
            double currentBeat = 1.0;
            calculateBarBeat(ppq, num, den, currentBar, currentBeat);
            bar_.store(currentBar, std::memory_order_relaxed);
            beat_.store(currentBeat, std::memory_order_relaxed);

            // MuseScore 4 Quirks Detection:
            // When MuseScore transport is in Stop, user clicking a note jumps `seconds` discontinuously.
            if (!hostIsPlaying && std::abs(seconds - lastObservedHostSec_) > 0.005)
            {
                // Discontinuous note/bar jump while in stop!
                // Immediately cancel free-scrub and re-lock to this position!
                isFreeScrubActive_.store(false, std::memory_order_release);
                isLocalPlaying_.store(false, std::memory_order_release);
                isLockedToHost_.store(true, std::memory_order_release);
            }
            lastObservedHostSec_ = seconds;

            // Smart Handoff:
            // If host starts playing, ALWAYS cancel free scrub and slave to host!
            if (hostIsPlaying)
            {
                if (isFreeScrubActive_.load(std::memory_order_relaxed) || isLocalPlaying_.load(std::memory_order_relaxed))
                {
                    isFreeScrubActive_.store(false, std::memory_order_release);
                    isLocalPlaying_.store(false, std::memory_order_release);
                }
                isLockedToHost_.store(true, std::memory_order_release);
            }

            isPlaying_.store(hostIsPlaying, std::memory_order_relaxed);
            isLooping_.store(hostIsLooping, std::memory_order_relaxed);
            isRecording_.store(hostIsRecording, std::memory_order_relaxed);
            timeInSamples_.store(samples, std::memory_order_relaxed);
            timeInSeconds_.store(seconds, std::memory_order_relaxed);

            lastHostSample_ = samples;
            lastHostTimeSec_ = seconds;
            return;
        }
    }

    // Fallback if host does not provide playhead
    if (buffer.getNumSamples() > 0 && currentSampleRate > 0.0)
    {
        accumulatedSampleTimeSec_ += static_cast<double>(buffer.getNumSamples()) / currentSampleRate;
        timeInSeconds_.store(accumulatedSampleTimeSec_, std::memory_order_relaxed);
    }
}

template void TransportSyncEngine::processAudioBlock<float>(const juce::AudioPlayHead*, const juce::AudioBuffer<float>&, const juce::MidiBuffer&, double) noexcept;
template void TransportSyncEngine::processAudioBlock<double>(const juce::AudioPlayHead*, const juce::AudioBuffer<double>&, const juce::MidiBuffer&, double) noexcept;

TransportSnapshot TransportSyncEngine::getSnapshot() const noexcept
{
    TransportSnapshot snap;
    snap.sampleRate = sampleRate_.load(std::memory_order_relaxed);
    snap.videoOffsetSeconds = videoOffsetSeconds_.load(std::memory_order_relaxed);
    snap.smpteStartTimeSeconds = smpteStartTimeSeconds_.load(std::memory_order_relaxed);
    snap.movieStartOffsetSeconds = movieStartOffsetSeconds_.load(std::memory_order_relaxed);
    snap.displayLatencySeconds = displayLatencySeconds_.load(std::memory_order_relaxed);
    snap.bpm = bpm_.load(std::memory_order_relaxed);
    snap.timeSigNumerator = timeSigNumerator_.load(std::memory_order_relaxed);
    snap.timeSigDenominator = timeSigDenominator_.load(std::memory_order_relaxed);
    snap.syncMode = syncMode_.load(std::memory_order_relaxed);

    bool freeScrub = isFreeScrubActive_.load(std::memory_order_relaxed);
    bool localPlay = isLocalPlaying_.load(std::memory_order_relaxed);

    if (freeScrub)
    {
        snap.isPlaying = false;
        snap.timeInSeconds = freeScrubTime_.load(std::memory_order_relaxed);
        snap.isLockedToHost = false;
        snap.activeSource = "Free Scrub (Stop Mode)";

        double currentBpm = snap.bpm > 0.0 ? snap.bpm : 120.0;
        snap.ppqPosition = (snap.timeInSeconds * currentBpm) / 60.0;
        calculateBarBeat(snap.ppqPosition, snap.timeSigNumerator, snap.timeSigDenominator, snap.bar, snap.beat);
    }
    else if (localPlay)
    {
        double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        double elapsed = now - localPlayStartWallTime_.load(std::memory_order_relaxed);
        snap.isPlaying = true;
        snap.timeInSeconds = localPlayStartPts_.load(std::memory_order_relaxed) + elapsed;
        snap.isLockedToHost = false;
        snap.activeSource = "Local Preview Playback";

        double currentBpm = snap.bpm > 0.0 ? snap.bpm : 120.0;
        snap.ppqPosition = (snap.timeInSeconds * currentBpm) / 60.0;
        calculateBarBeat(snap.ppqPosition, snap.timeSigNumerator, snap.timeSigDenominator, snap.bar, snap.beat);
    }
    else
    {
        snap.isPlaying = isPlaying_.load(std::memory_order_relaxed);
        snap.isLooping = isLooping_.load(std::memory_order_relaxed);
        snap.isRecording = isRecording_.load(std::memory_order_relaxed);
        snap.timeInSamples = timeInSamples_.load(std::memory_order_relaxed);
        snap.timeInSeconds = timeInSeconds_.load(std::memory_order_relaxed);
        snap.ppqPosition = ppqPosition_.load(std::memory_order_relaxed);
        snap.bar = bar_.load(std::memory_order_relaxed);
        snap.beat = beat_.load(std::memory_order_relaxed);
        snap.isLockedToHost = isLockedToHost_.load(std::memory_order_relaxed);
        snap.activeSource = snap.isPlaying ? "Host DAW Playing" : "Host DAW Stopped";
    }

    // Apply trim constraints if configured
    double trimIn = trimInSeconds_.load(std::memory_order_relaxed);
    double trimOut = trimOutSeconds_.load(std::memory_order_relaxed);
    if (trimOut > trimIn && trimOut > 0.0)
    {
        snap.timeInSeconds = juce::jlimit(trimIn, trimOut, snap.timeInSeconds);
    }

    return snap;
}

bool TransportSyncEngine::checkWatchdog(double currentWallTimeSeconds) noexcept
{
    // If audio thread has not fired for > 150ms, host audio processing is paused
    const double lastWall = lastBlockWallTime_.load(std::memory_order_relaxed);
    if (lastWall > 0.0 && (currentWallTimeSeconds - lastWall) > 0.15)
    {
        if (isPlaying_.exchange(false, std::memory_order_acq_rel))
        {
            return true;
        }
    }
    return false;
}

void TransportSyncEngine::freeScrubToTime(double timeSeconds) noexcept
{
    // Auto Free-Scrub in Stop: switch to free scrub immediately
    isLocalPlaying_.store(false, std::memory_order_release);
    freeScrubTime_.store(std::max(0.0, timeSeconds), std::memory_order_release);
    isFreeScrubActive_.store(true, std::memory_order_release);
    isLockedToHost_.store(false, std::memory_order_release);
}

void TransportSyncEngine::stepFrames(int frameDelta, SMPTEFrameRate rate) noexcept
{
    double currentPts = getSnapshot().timeInSeconds;
    double frameDuration = 1.0 / getFrameRateValue(rate);
    double newPts = std::max(0.0, currentPts + (frameDelta * frameDuration));
    freeScrubToTime(newPts);
}

void TransportSyncEngine::stepSeconds(double secondsDelta) noexcept
{
    double currentPts = getSnapshot().timeInSeconds;
    double newPts = std::max(0.0, currentPts + secondsDelta);
    freeScrubToTime(newPts);
}

void TransportSyncEngine::setLocalPlaying(bool playing) noexcept
{
    if (playing)
    {
        double currentPts = getSnapshot().timeInSeconds;
        localPlayStartPts_.store(currentPts, std::memory_order_release);
        localPlayStartWallTime_.store(juce::Time::getMillisecondCounterHiRes() * 0.001, std::memory_order_release);
        isLocalPlaying_.store(true, std::memory_order_release);
        isFreeScrubActive_.store(false, std::memory_order_release);
        isLockedToHost_.store(false, std::memory_order_release);
    }
    else
    {
        if (isLocalPlaying_.load(std::memory_order_relaxed))
        {
            double currentPts = getSnapshot().timeInSeconds;
            freeScrubTime_.store(currentPts, std::memory_order_release);
            isFreeScrubActive_.store(true, std::memory_order_release);
            isLocalPlaying_.store(false, std::memory_order_release);
        }
    }
}

void TransportSyncEngine::forceReLockToHost() noexcept
{
    isFreeScrubActive_.store(false, std::memory_order_release);
    isLocalPlaying_.store(false, std::memory_order_release);
    isLockedToHost_.store(true, std::memory_order_release);
}

void TransportSyncEngine::setVideoOffsetSeconds(double offsetSeconds) noexcept
{
    videoOffsetSeconds_.store(offsetSeconds, std::memory_order_release);
}

double TransportSyncEngine::getVideoOffsetSeconds() const noexcept
{
    return videoOffsetSeconds_.load(std::memory_order_acquire);
}

void TransportSyncEngine::setSmpteStartTime(double smpteStartSeconds) noexcept
{
    smpteStartTimeSeconds_.store(smpteStartSeconds, std::memory_order_release);
}

double TransportSyncEngine::getSmpteStartTime() const noexcept
{
    return smpteStartTimeSeconds_.load(std::memory_order_acquire);
}

void TransportSyncEngine::setMovieStartOffset(double movieOffsetSeconds) noexcept
{
    movieStartOffsetSeconds_.store(movieOffsetSeconds, std::memory_order_release);
}

double TransportSyncEngine::getMovieStartOffset() const noexcept
{
    return movieStartOffsetSeconds_.load(std::memory_order_acquire);
}

void TransportSyncEngine::setDisplayLatencyCompensation(double latencySeconds) noexcept
{
    displayLatencySeconds_.store(juce::jlimit(-0.5, 0.5, latencySeconds), std::memory_order_release);
}

double TransportSyncEngine::getDisplayLatencyCompensation() const noexcept
{
    return displayLatencySeconds_.load(std::memory_order_acquire);
}

void TransportSyncEngine::setTrimRange(double trimInSeconds, double trimOutSeconds) noexcept
{
    trimInSeconds_.store(std::max(0.0, trimInSeconds), std::memory_order_release);
    trimOutSeconds_.store(std::max(0.0, trimOutSeconds), std::memory_order_release);
}

double TransportSyncEngine::getTrimInSeconds() const noexcept
{
    return trimInSeconds_.load(std::memory_order_acquire);
}

double TransportSyncEngine::getTrimOutSeconds() const noexcept
{
    return trimOutSeconds_.load(std::memory_order_acquire);
}

void TransportSyncEngine::setSyncMode(SyncMode mode) noexcept
{
    syncMode_.store(mode, std::memory_order_release);
}

SyncMode TransportSyncEngine::getSyncMode() const noexcept
{
    return syncMode_.load(std::memory_order_acquire);
}

void TransportSyncEngine::setScoreTempoBpm(double bpm) noexcept
{
    if (bpm > 0.0)
    {
        bpm_.store(bpm, std::memory_order_release);
    }
}

double TransportSyncEngine::getScoreTempoBpm() const noexcept
{
    return bpm_.load(std::memory_order_acquire);
}

void TransportSyncEngine::setScoreTimeSignature(int numerator, int denominator) noexcept
{
    if (numerator > 0) timeSigNumerator_.store(numerator, std::memory_order_release);
    if (denominator > 0) timeSigDenominator_.store(denominator, std::memory_order_release);
}

int TransportSyncEngine::getTimeSigNumerator() const noexcept
{
    return timeSigNumerator_.load(std::memory_order_acquire);
}

int TransportSyncEngine::getTimeSigDenominator() const noexcept
{
    return timeSigDenominator_.load(std::memory_order_acquire);
}

void TransportSyncEngine::calculateBarBeat(double ppq, int timeSigNum, int timeSigDenom,
                                          int& outBar, double& outBeat) noexcept
{
    if (timeSigNum <= 0) timeSigNum = 4;
    if (timeSigDenom <= 0) timeSigDenom = 4;

    double beatsPerBar = static_cast<double>(timeSigNum) * (4.0 / static_cast<double>(timeSigDenom));
    if (beatsPerBar <= 0.0) beatsPerBar = 4.0;

    double safePpq = std::max(0.0, ppq);
    int barIndex = static_cast<int>(std::floor(safePpq / beatsPerBar));
    outBar = barIndex + 1; // 1-based bar
    double beatInBar = std::fmod(safePpq, beatsPerBar);
    outBeat = 1.0 + beatInBar; // 1-based beat
}

juce::String TransportSyncEngine::formatMeasureBeat(int bar, double beat, int timeSigNum, int timeSigDenom)
{
    int wholeBeat = static_cast<int>(beat);
    int subBeat = static_cast<int>((beat - wholeBeat) * 100.0);
    return juce::String::formatted("%03d | %02d | %02d  (%d/%d)",
                                   bar, wholeBeat, subBeat, timeSigNum, timeSigDenom);
}

} // namespace MScoreVideoSync
