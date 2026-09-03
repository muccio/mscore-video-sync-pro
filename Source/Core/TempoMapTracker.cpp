#include "TempoMapTracker.h"
#include <cmath>
#include <algorithm>

namespace MScoreVideoSync
{

TempoMapTracker::TempoMapTracker() noexcept
{
}

template <typename FloatType>
void TempoMapTracker::processAudioBlock(const juce::AudioPlayHead* playHead,
                                       const juce::AudioBuffer<FloatType>& buffer,
                                       const juce::MidiBuffer& midiMessages,
                                       double currentSampleRate) noexcept
{
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    lastBlockWallTime_ = now;

    double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
    sampleRate_.store(sr, std::memory_order_relaxed);

    int numSamples = buffer.getNumSamples();
    float magnitude = (numSamples > 0) ? static_cast<float>(buffer.getMagnitude(0, numSamples)) : 0.0f;
    audioLevel_.store(magnitude, std::memory_order_relaxed);

    // -------------------------------------------------------------------------
    // 1. Evaluate Host / DAW Position (Cubase, Logic, MuseScore 4 projectTimeSamples)
    // -------------------------------------------------------------------------
    bool hostIsAdvancing = false;
    double hostTimeSec = 0.0;
    double currentBpm = bpm_.load(std::memory_order_relaxed);

    if (playHead != nullptr)
    {
        if (auto posInfoOpt = playHead->getPosition())
        {
            bool isHostPlaying = posInfoOpt->getIsPlaying();
            if (isHostPlaying)
            {
                hostIsAdvancing = true;
            }

            isRecording_.store(posInfoOpt->getIsRecording(), std::memory_order_relaxed);
            isLooping_.store(posInfoOpt->getIsLooping(), std::memory_order_relaxed);

            if (!isTempoUserOverridden_.load(std::memory_order_relaxed))
            {
                if (auto bpmOpt = posInfoOpt->getBpm())
                {
                    if (*bpmOpt > 0.0)
                    {
                        currentBpm = *bpmOpt;
                        bpm_.store(currentBpm, std::memory_order_relaxed);
                    }
                }

                if (auto timeSigOpt = posInfoOpt->getTimeSignature())
                {
                    timeSigNumerator_.store(timeSigOpt->numerator > 0 ? timeSigOpt->numerator : 4, std::memory_order_relaxed);
                    timeSigDenominator_.store(timeSigOpt->denominator > 0 ? timeSigOpt->denominator : 4, std::memory_order_relaxed);
                }
            }

            bool gotTime = false;
            if (auto samplesOpt = posInfoOpt->getTimeInSamples())
            {
                int64_t currentSamples = *samplesOpt;
                hostTimeSec = static_cast<double>(currentSamples) / sr;
                // Only treat position changes as playback advancement if host is playing or contiguously incrementing
                if (isHostPlaying || (lastSamplePosition_ >= 0 && currentSamples > lastSamplePosition_ && (currentSamples - lastSamplePosition_) <= numSamples * 2))
                {
                    hostIsAdvancing = true;
                }
                lastSamplePosition_ = currentSamples;
                gotTime = true;
            }
            else if (auto secOpt = posInfoOpt->getTimeInSeconds())
            {
                hostTimeSec = *secOpt;
                if (isHostPlaying || (lastHostTimeSec_ >= 0.0 && (hostTimeSec - lastHostTimeSec_) > 0.0001 && (hostTimeSec - lastHostTimeSec_) < 0.2))
                {
                    hostIsAdvancing = true;
                }
                lastHostTimeSec_ = hostTimeSec;
                gotTime = true;
            }

            if (auto ppqOpt = posInfoOpt->getPpqPosition())
            {
                ppqPosition_.store(*ppqOpt, std::memory_order_relaxed);
                if (!gotTime)
                {
                    hostTimeSec = (*ppqOpt * 60.0) / currentBpm;
                    if (isHostPlaying || (lastHostTimeSec_ >= 0.0 && (hostTimeSec - lastHostTimeSec_) > 0.0001 && (hostTimeSec - lastHostTimeSec_) < 0.2))
                    {
                        hostIsAdvancing = true;
                    }
                    lastHostTimeSec_ = hostTimeSec;
                    gotTime = true;
                }
            }

            if (isHostPlaying && hostIsAdvancing)
            {
                activeFramesRemaining_ = 30; // Sustain host position tracking for 30 blocks (~300ms) while playing
            }
            else
            {
                activeFramesRemaining_ = 0; // Stopped / paused / note jump
            }
        }
    }

    if (activeFramesRemaining_ > 0)
    {
        --activeFramesRemaining_;
    }

    // -------------------------------------------------------------------------
    // 2. Evaluate MIDI Transport & Clock
    // -------------------------------------------------------------------------
    if (!midiMessages.isEmpty())
    {
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            if (msg.isMidiStart())
            {
                midiPlaying_ = true;
                midiClockPpq_ = 0.0;
                midiActive_.store(true, std::memory_order_relaxed);
            }
            else if (msg.isMidiContinue())
            {
                midiPlaying_ = true;
                midiActive_.store(true, std::memory_order_relaxed);
            }
            else if (msg.isMidiStop())
            {
                midiPlaying_ = false;
            }
            else if (msg.isSongPositionPointer())
            {
                const auto* raw = msg.getRawData();
                int spp = (static_cast<int>(raw[2]) << 7) | static_cast<int>(raw[1]);
                midiClockPpq_ = static_cast<double>(spp) * 0.25;
                midiPlaying_ = true;
                midiActive_.store(true, std::memory_order_relaxed);
            }
            else if (msg.isMidiClock())
            {
                if (midiPlaying_)
                {
                    midiClockPpq_ += (1.0 / 24.0);
                    midiActive_.store(true, std::memory_order_relaxed);
                }
            }
            else if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                midiActive_.store(true, std::memory_order_relaxed);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 3. Audio Block Sample Accumulator (MuseScore 4 Real-Time Audio Streaming)
    // -------------------------------------------------------------------------
    if (numSamples > 0)
    {
        accumulatedSampleTimeSec_ += static_cast<double>(numSamples) / sr;
    }

    // -------------------------------------------------------------------------
    // 4. Multi-Modal Arbitration
    // -------------------------------------------------------------------------
    SyncMode mode = syncMode_.load(std::memory_order_relaxed);

    bool finalPlaying = false;
    double finalTimeSec = 0.0;
    int selectedSource = 0; // 0: Stopped, 1: Host/DAW Samples, 2: Audio Stream, 3: MIDI, 4: Manual

    if (mode == SyncMode::DAWPlayHead)
    {
        finalPlaying = (activeFramesRemaining_ > 0);
        finalTimeSec = hostTimeSec;
        selectedSource = finalPlaying ? 1 : 0;
    }
    else if (mode == SyncMode::AudioStream)
    {
        finalPlaying = (activeFramesRemaining_ > 0 || numSamples > 0);
        finalTimeSec = accumulatedSampleTimeSec_;
        selectedSource = 2;
    }
    else if (mode == SyncMode::MIDIClock)
    {
        finalPlaying = midiPlaying_;
        finalTimeSec = (midiClockPpq_ * 60.0) / currentBpm;
        selectedSource = 3;
    }
    else // SyncMode::Auto (Smart Best-Fit)
    {
        if (activeFramesRemaining_ > 0)
        {
            finalPlaying = true;
            finalTimeSec = hostTimeSec;
            selectedSource = 1; // Host samples / DAW PlayHead
        }
        else if (midiPlaying_ || midiClockPpq_ > 0.0)
        {
            finalPlaying = midiPlaying_;
            finalTimeSec = (midiClockPpq_ * 60.0) / currentBpm;
            selectedSource = 3; // MIDI Clock
        }
        else
        {
            // Stopped / paused
            finalPlaying = false;
            finalTimeSec = hostTimeSec;
            selectedSource = 0;
        }
    }

    activeSource_.store(selectedSource, std::memory_order_relaxed);
    isPlaying_.store(finalPlaying, std::memory_order_release);
    timeInSeconds_.store(finalTimeSec, std::memory_order_release);
    timeInSamples_.store(static_cast<int64_t>(finalTimeSec * sr), std::memory_order_relaxed);

    if (selectedSource != 1 && currentBpm > 0.0)
    {
        ppqPosition_.store((finalTimeSec * currentBpm) / 60.0, std::memory_order_relaxed);
    }
}

template void TempoMapTracker::processAudioBlock<float>(const juce::AudioPlayHead*, const juce::AudioBuffer<float>&, const juce::MidiBuffer&, double) noexcept;
template void TempoMapTracker::processAudioBlock<double>(const juce::AudioPlayHead*, const juce::AudioBuffer<double>&, const juce::MidiBuffer&, double) noexcept;

void TempoMapTracker::checkWatchdog(double currentWallTimeSeconds) noexcept
{
    if (lastBlockWallTime_ > 0.0 && (currentWallTimeSeconds - lastBlockWallTime_) > 0.20)
    {
        // No audio blocks received from host in > 200ms -> Host is paused/stopped!
        isPlaying_.store(false, std::memory_order_release);
        activeSource_.store(0, std::memory_order_relaxed);
    }
}

void TempoMapTracker::updateFromPlayHead(const juce::AudioPlayHead::PositionInfo& posInfo, double currentSampleRate) noexcept
{
    double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    sampleRate_.store(sr, std::memory_order_relaxed);
    isPlaying_.store(posInfo.getIsPlaying(), std::memory_order_relaxed);
    if (auto sec = posInfo.getTimeInSeconds())
        timeInSeconds_.store(*sec, std::memory_order_release);
}

void TempoMapTracker::update(const TransportSnapshot& snapshot) noexcept
{
    isPlaying_.store(snapshot.isPlaying, std::memory_order_relaxed);
    isRecording_.store(snapshot.isRecording, std::memory_order_relaxed);
    isLooping_.store(snapshot.isLooping, std::memory_order_relaxed);
    timeInSeconds_.store(snapshot.timeInSeconds, std::memory_order_relaxed);
    timeInSamples_.store(snapshot.timeInSamples, std::memory_order_relaxed);
    bpm_.store(snapshot.bpm, std::memory_order_relaxed);
    ppqPosition_.store(snapshot.ppqPosition, std::memory_order_relaxed);
    timeSigNumerator_.store(snapshot.timeSigNumerator, std::memory_order_relaxed);
    timeSigDenominator_.store(snapshot.timeSigDenominator, std::memory_order_relaxed);
    sampleRate_.store(snapshot.sampleRate, std::memory_order_relaxed);
    videoOffsetSeconds_.store(snapshot.videoOffsetSeconds, std::memory_order_release);
    syncMode_.store(snapshot.syncMode, std::memory_order_relaxed);
}

TransportSnapshot TempoMapTracker::getSnapshot() const noexcept
{
    TransportSnapshot snap;
    snap.timeInSeconds = timeInSeconds_.load(std::memory_order_acquire);
    snap.isPlaying = isPlaying_.load(std::memory_order_acquire);
    snap.isRecording = isRecording_.load(std::memory_order_relaxed);
    snap.isLooping = isLooping_.load(std::memory_order_relaxed);
    snap.timeInSamples = timeInSamples_.load(std::memory_order_relaxed);
    snap.bpm = bpm_.load(std::memory_order_relaxed);
    snap.ppqPosition = ppqPosition_.load(std::memory_order_relaxed);
    snap.timeSigNumerator = timeSigNumerator_.load(std::memory_order_relaxed);
    snap.timeSigDenominator = timeSigDenominator_.load(std::memory_order_relaxed);
    snap.sampleRate = sampleRate_.load(std::memory_order_relaxed);
    snap.videoOffsetSeconds = videoOffsetSeconds_.load(std::memory_order_relaxed);
    snap.syncMode = syncMode_.load(std::memory_order_relaxed);
    snap.audioLevel = audioLevel_.load(std::memory_order_relaxed);
    snap.midiActive = midiActive_.load(std::memory_order_relaxed);

    int src = activeSource_.load(std::memory_order_relaxed);
    switch (src)
    {
        case 1: snap.activeSource = "Host Samples"; break;
        case 2: snap.activeSource = "MuseScore Audio Clock"; break;
        case 3: snap.activeSource = "MIDI Clock"; break;
        case 4: snap.activeSource = "Manual"; break;
        default: snap.activeSource = "Stopped"; break;
    }

    return snap;
}

void TempoMapTracker::setVideoOffsetSeconds(double offsetSeconds) noexcept
{
    double clamped = juce::jlimit(-30.0, 30.0, offsetSeconds);
    videoOffsetSeconds_.store(clamped, std::memory_order_release);
}

double TempoMapTracker::getVideoOffsetSeconds() const noexcept
{
    return videoOffsetSeconds_.load(std::memory_order_acquire);
}

void TempoMapTracker::setSyncMode(SyncMode mode) noexcept
{
    syncMode_.store(mode, std::memory_order_release);
}

SyncMode TempoMapTracker::getSyncMode() const noexcept
{
    return syncMode_.load(std::memory_order_acquire);
}

void TempoMapTracker::setScoreTempoBpm(double bpm) noexcept
{
    double clamped = juce::jlimit(20.0, 360.0, bpm);
    bpm_.store(clamped, std::memory_order_release);
    isTempoUserOverridden_.store(true, std::memory_order_release);
}

double TempoMapTracker::getScoreTempoBpm() const noexcept
{
    return bpm_.load(std::memory_order_acquire);
}

void TempoMapTracker::setScoreTimeSignature(int numerator, int denominator) noexcept
{
    int num = juce::jlimit(1, 32, numerator);
    int den = juce::jlimit(1, 32, denominator);
    timeSigNumerator_.store(num, std::memory_order_release);
    timeSigDenominator_.store(den, std::memory_order_release);
    isTempoUserOverridden_.store(true, std::memory_order_release);
}

int TempoMapTracker::getTimeSigNumerator() const noexcept
{
    return timeSigNumerator_.load(std::memory_order_acquire);
}

int TempoMapTracker::getTimeSigDenominator() const noexcept
{
    return timeSigDenominator_.load(std::memory_order_acquire);
}

void TempoMapTracker::setTempoUserOverridden(bool overridden) noexcept
{
    isTempoUserOverridden_.store(overridden, std::memory_order_release);
}

bool TempoMapTracker::isTempoUserOverridden() const noexcept
{
    return isTempoUserOverridden_.load(std::memory_order_acquire);
}

void TempoMapTracker::resetInternalClock() noexcept
{
    accumulatedSampleTimeSec_ = 0.0;
    lastSamplePosition_ = -1;
    lastHostTimeSec_ = -1.0;
    activeFramesRemaining_ = 0;
    midiClockPpq_ = 0.0;
    midiPlaying_ = false;
    timeInSeconds_.store(0.0, std::memory_order_release);
    timeInSamples_.store(0, std::memory_order_relaxed);
    ppqPosition_.store(0.0, std::memory_order_relaxed);
    isPlaying_.store(false, std::memory_order_release);
    activeSource_.store(0, std::memory_order_relaxed);
}

void TempoMapTracker::setManualTime(double timeSeconds) noexcept
{
    accumulatedSampleTimeSec_ = std::max(0.0, timeSeconds);
    double bpm = bpm_.load(std::memory_order_relaxed);
    if (bpm > 0.0)
    {
        midiClockPpq_ = (accumulatedSampleTimeSec_ * bpm) / 60.0;
    }
    timeInSeconds_.store(accumulatedSampleTimeSec_, std::memory_order_release);
}

void TempoMapTracker::setManualPlaying(bool playing) noexcept
{
    isPlaying_.store(playing, std::memory_order_release);
    activeSource_.store(playing ? 4 : 0, std::memory_order_relaxed);
}

void TempoMapTracker::calculateBarBeat(double ppq, int timeSigNum, int timeSigDenom,
                                      int& outBar, int& outBeat, double& outFraction) noexcept
{
    if (timeSigNum <= 0) timeSigNum = 4;
    if (timeSigDenom <= 0) timeSigDenom = 4;

    double quarterNotesPerBeat = 4.0 / static_cast<double>(timeSigDenom);
    double quarterNotesPerBar = quarterNotesPerBeat * static_cast<double>(timeSigNum);

    if (quarterNotesPerBar <= 0.0)
        quarterNotesPerBar = 4.0;

    double positivePpq = std::max(0.0, ppq);
    int barIndex = static_cast<int>(positivePpq / quarterNotesPerBar);
    double ppqInBar = positivePpq - (barIndex * quarterNotesPerBar);

    int beatIndex = static_cast<int>(ppqInBar / quarterNotesPerBeat);
    double ppqInBeat = ppqInBar - (beatIndex * quarterNotesPerBeat);

    outBar = barIndex + 1;
    outBeat = beatIndex + 1;
    outFraction = (quarterNotesPerBeat > 0.0) ? (ppqInBeat / quarterNotesPerBeat) : 0.0;
}

juce::String TempoMapTracker::formatMeasureBeat(double ppq, int timeSigNum, int timeSigDenom, bool concise)
{
    int bar = 1;
    int beat = 1;
    double frac = 0.0;
    calculateBarBeat(ppq, timeSigNum, timeSigDenom, bar, beat, frac);

    int subBeat = static_cast<int>(frac * 100.0);

    if (concise)
    {
        return juce::String::formatted("M %d.%d.%02d", bar, beat, subBeat);
    }
    return juce::String::formatted("Bar %d, Beat %d.%02d", bar, beat, subBeat);
}

double TempoMapTracker::calculateTargetPts(double hostSeconds) const noexcept
{
    double offset = videoOffsetSeconds_.load(std::memory_order_relaxed);
    return std::max(0.0, hostSeconds + offset);
}

double TempoMapTracker::getInterpolatedPts(double hostSeconds, double elapsedSecondsSinceBlock, bool isPlaying) const noexcept
{
    double baseTime = hostSeconds;
    if (isPlaying && elapsedSecondsSinceBlock > 0.0 && elapsedSecondsSinceBlock < 0.5)
    {
        baseTime += elapsedSecondsSinceBlock;
    }
    return calculateTargetPts(baseTime);
}

} // namespace MScoreVideoSync
