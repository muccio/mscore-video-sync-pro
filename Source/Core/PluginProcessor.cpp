#include "PluginProcessor.h"
#include "../UI/PluginEditor.h"

namespace MScoreVideoSync
{

MScoreVideoSyncProcessor::MScoreVideoSyncProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter(paramOffset_ = new juce::AudioParameterFloat(
        juce::ParameterID("videoOffset", 1),
        "Video Offset",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("s")));

    juce::StringArray smpteNames;
    for (int i = 0; i < static_cast<int>(SMPTEFrameRate::Count); ++i)
        smpteNames.add(getFrameRateLabel(static_cast<SMPTEFrameRate>(i)));

    addParameter(paramSmpteRate_ = new juce::AudioParameterChoice(
        juce::ParameterID("smpteRate", 1),
        "SMPTE Rate",
        smpteNames,
        static_cast<int>(SMPTEFrameRate::FPS_24)));

    juce::StringArray syncNames;
    for (int i = 0; i < static_cast<int>(SyncMode::Count); ++i)
        syncNames.add(getSyncModeLabel(static_cast<SyncMode>(i)));

    addParameter(paramSyncMode_ = new juce::AudioParameterChoice(
        juce::ParameterID("syncMode", 1),
        "Sync Mode",
        syncNames,
        static_cast<int>(SyncMode::HostLockAutoScrub)));

    // Start local IPC shared memory producer
    ipc_.startProducer();

    // Start network OSC bridge
    networkBridge_.setCommandListener(this);
    networkBridge_.startBroadcaster("127.0.0.1", 9000);
    networkBridge_.startReceiver(9001);

    loadPersistentSettings();
}

MScoreVideoSyncProcessor::~MScoreVideoSyncProcessor()
{
    networkBridge_.stopBridge();
    ipc_.close();
    floatingWindow_ = nullptr;
}

const juce::String MScoreVideoSyncProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MScoreVideoSyncProcessor::acceptsMidi() const
{
    return true;
}

bool MScoreVideoSyncProcessor::producesMidi() const
{
    return true;
}

bool MScoreVideoSyncProcessor::isMidiEffect() const
{
    return false;
}

double MScoreVideoSyncProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MScoreVideoSyncProcessor::getNumPrograms()
{
    return 1;
}

int MScoreVideoSyncProcessor::getCurrentProgram()
{
    return 0;
}

void MScoreVideoSyncProcessor::setCurrentProgram(int /*index*/)
{
}

const juce::String MScoreVideoSyncProcessor::getProgramName(int /*index*/)
{
    return {};
}

void MScoreVideoSyncProcessor::changeProgramName(int /*index*/, const juce::String& /*newName*/)
{
}

void MScoreVideoSyncProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    syncEngine_.setScoreTempoBpm(120.0);
    audioPlayer_.prepareToPlay(sampleRate);
    twoPopGen_.prepare(sampleRate);

    if (!decoder_.isThreadRunning())
    {
        decoder_.startThread(juce::Thread::Priority::high);
    }
}

void MScoreVideoSyncProcessor::releaseResources()
{
    // Threads remain active so scrubbing, score navigation, and floating window continue working
}

bool MScoreVideoSyncProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
    {
        return false;
    }

    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::disabled())
    {
        return false;
    }

    return true;
}

void MScoreVideoSyncProcessor::showFloatingWindow(bool show)
{
    if (show)
    {
        if (floatingWindow_ == nullptr)
        {
            floatingWindow_ = std::make_unique<FloatingVideoWindow>("MScore Video Pro - Secondary Viewport", decoder_, markerModel_);
        }
        floatingWindow_->setVisible(true);
        floatingWindow_->toFront(true);
    }
    else
    {
        if (floatingWindow_ != nullptr)
        {
            floatingWindow_->setVisible(false);
        }
    }
}

bool MScoreVideoSyncProcessor::isFloatingWindowVisible() const
{
    return floatingWindow_ != nullptr && floatingWindow_->isVisible();
}

template <typename FloatType>
void MScoreVideoSyncProcessor::processBlockInternal(juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midiMessages)
{
    // 1. Update Transport Engine (Zero alloc, Zero locks)
    syncEngine_.processAudioBlock(getPlayHead(), buffer, midiMessages, getSampleRate());
    auto snapshot = syncEngine_.getSnapshot();

    // 2. Transmit MIDI locate if requested
    double locateTime = pendingMidiLocateTime_.exchange(-1.0, std::memory_order_acq_rel);
    if (locateTime >= 0.0)
    {
        double bpm = syncEngine_.getScoreTempoBpm();
        double ppq = (locateTime * bpm) / 60.0;
        int midiBeats = static_cast<int>(std::round(ppq * 4.0));
        int lsb = midiBeats & 0x7F;
        int msb = (midiBeats >> 7) & 0x7F;
        midiMessages.addEvent(juce::MidiMessage(0xF2, lsb, msb), 0);
    }

    double currentPts = snapshot.getTargetVideoPts();
    double lastPts = lastProcessedPts_.load(std::memory_order_relaxed);

    // 3. Trigger 2-Pop audio cue clicks on hit frames
    if (snapshot.isPlaying && lastPts >= 0.0 && currentPts > lastPts && (currentPts - lastPts) < 1.0)
    {
        auto markers = markerModel_.getMarkersSorted();
        for (const auto& m : markers)
        {
            if (m.twoPopAudioEnabled)
            {
                double hitPts = m.getEffectivePts(snapshot.bpm, 4);
                if (hitPts >= lastPts && hitPts < currentPts)
                {
                    twoPopGen_.trigger(0.041666);
                }
            }
        }
    }
    lastProcessedPts_.store(currentPts, std::memory_order_relaxed);

    // 4. Mix embedded audio stream into plugin output
    audioPlayer_.processBlockAdding(buffer, buffer.getNumSamples());

    // 5. Mix audible 2-Pop cue synth
    twoPopGen_.processAdding(buffer, buffer.getNumSamples());

    // 6. Push state to Video Engine
    decoder_.setPlaybackState(snapshot.isPlaying, currentPts);
    audioPlayer_.setPlaybackState(snapshot.isPlaying, currentPts);

    // 7. Write to Local Shared Memory IPC for Standalone/Satellite
    ipc_.writeProducerState(snapshot, lastLoadedVideoPath_, smpteRate_.load(std::memory_order_relaxed), syncEngine_.getSyncMode());

    // 8. Update network bridge telemetry
    networkBridge_.updateTelemetry(snapshot);

    // 9. Update floating window if open
    if (floatingWindow_ && floatingWindow_->isVisible())
    {
        floatingWindow_->updateSnapshot(snapshot, smpteRate_.load(std::memory_order_relaxed));
    }
}

void MScoreVideoSyncProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    processBlockInternal(buffer, midiMessages);
}

void MScoreVideoSyncProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    processBlockInternal(buffer, midiMessages);
}

bool MScoreVideoSyncProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* MScoreVideoSyncProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void MScoreVideoSyncProcessor::requestHostLocate(double timeSeconds)
{
    pendingMidiLocateTime_.store(timeSeconds, std::memory_order_release);
    decoder_.requestSeek(timeSeconds);
    audioPlayer_.requestSeek(timeSeconds);
}

void MScoreVideoSyncProcessor::onRemotePlay()
{
    syncEngine_.setLocalPlaying(true);
}

void MScoreVideoSyncProcessor::onRemotePause()
{
    syncEngine_.setLocalPlaying(false);
}

void MScoreVideoSyncProcessor::onRemoteStop()
{
    syncEngine_.setLocalPlaying(false);
}

void MScoreVideoSyncProcessor::onRemoteLocate(double timeSeconds)
{
    requestHostLocate(timeSeconds);
}

bool MScoreVideoSyncProcessor::loadVideoFile(const juce::File& file)
{
    bool videoOk = decoder_.openFile(file);
    if (videoOk)
    {
        audioPlayer_.openFile(file, getSampleRate());

        auto meta = decoder_.getMetadata();
        SMPTEFrameRate detected = detectFrameRateFromFps(meta.frameRate);
        setSMPTEFrameRate(detected);

        lastLoadedVideoPath_ = file.getFullPathName();
        savePersistentSettings();
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }
    return videoOk;
}

void MScoreVideoSyncProcessor::closeVideoFile()
{
    decoder_.closeFile();
    audioPlayer_.closeFile();
    lastLoadedVideoPath_ = {};
    savePersistentSettings();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

juce::String MScoreVideoSyncProcessor::getLoadedVideoPath() const
{
    return lastLoadedVideoPath_;
}

void MScoreVideoSyncProcessor::setVideoOffset(double offsetSeconds)
{
    videoOffsetSeconds_.store(offsetSeconds, std::memory_order_release);
    syncEngine_.setVideoOffsetSeconds(offsetSeconds);
    if (paramOffset_ != nullptr && std::abs(paramOffset_->get() - static_cast<float>(offsetSeconds)) > 0.0001f)
    {
        *paramOffset_ = static_cast<float>(offsetSeconds);
    }
    savePersistentSettings();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

double MScoreVideoSyncProcessor::getVideoOffset() const noexcept
{
    return videoOffsetSeconds_.load(std::memory_order_acquire);
}

void MScoreVideoSyncProcessor::setSMPTEFrameRate(SMPTEFrameRate rate)
{
    smpteRate_.store(rate, std::memory_order_release);
    if (paramSmpteRate_ != nullptr && paramSmpteRate_->getIndex() != static_cast<int>(rate))
    {
        *paramSmpteRate_ = static_cast<int>(rate);
    }
    savePersistentSettings();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

SMPTEFrameRate MScoreVideoSyncProcessor::getSMPTEFrameRate() const noexcept
{
    return smpteRate_.load(std::memory_order_acquire);
}

void MScoreVideoSyncProcessor::setSyncMode(SyncMode mode)
{
    syncEngine_.setSyncMode(mode);
    if (paramSyncMode_ != nullptr && paramSyncMode_->getIndex() != static_cast<int>(mode))
    {
        *paramSyncMode_ = static_cast<int>(mode);
    }
    savePersistentSettings();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

SyncMode MScoreVideoSyncProcessor::getSyncMode() const noexcept
{
    return syncEngine_.getSyncMode();
}

static juce::File getSettingsFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("MScoreVideoSyncPro");
    if (!dir.exists()) dir.createDirectory();
    return dir.getChildFile("settings.json");
}

void MScoreVideoSyncProcessor::savePersistentSettings()
{
    SerializedPluginState state;
    state.videoFilePath = lastLoadedVideoPath_;
    state.videoOffsetSeconds = videoOffsetSeconds_.load(std::memory_order_relaxed);
    state.smpteStartTimeSeconds = syncEngine_.getSmpteStartTime();
    state.movieStartOffsetSeconds = syncEngine_.getMovieStartOffset();
    state.displayLatencySeconds = syncEngine_.getDisplayLatencyCompensation();
    state.trimInSeconds = syncEngine_.getTrimInSeconds();
    state.trimOutSeconds = syncEngine_.getTrimOutSeconds();
    state.smpteRate = static_cast<int>(smpteRate_.load(std::memory_order_relaxed));
    state.syncMode = static_cast<int>(syncEngine_.getSyncMode());

    state.audioGainDb = audioPlayer_.getGainDecibels();
    state.isAudioMuted = audioPlayer_.isMuted();
    state.isAudioDimmed = audioPlayer_.isDimmed();

    state.isFloatingWindowVisible = isFloatingWindowVisible();
    if (floatingWindow_)
    {
        state.floatingWindowGeometry = floatingWindow_->getWindowGeometryString();
        state.isAlwaysOnTop = floatingWindow_->isAlwaysOnTopActive();
        state.isBorderless = floatingWindow_->isBorderlessActive();
        state.isClickThrough = floatingWindow_->isClickThroughActive();
    }

    state.markersJson = markerModel_.exportToJson();

    ProjectStateSerializer::saveToMvspFile(getSettingsFile(), state);
}

void MScoreVideoSyncProcessor::loadPersistentSettings()
{
    SerializedPluginState state;
    if (ProjectStateSerializer::loadFromMvspFile(getSettingsFile(), state))
    {
        setVideoOffset(state.videoOffsetSeconds);
        syncEngine_.setSmpteStartTime(state.smpteStartTimeSeconds);
        syncEngine_.setMovieStartOffset(state.movieStartOffsetSeconds);
        syncEngine_.setDisplayLatencyCompensation(state.displayLatencySeconds);
        syncEngine_.setTrimRange(state.trimInSeconds, state.trimOutSeconds);

        setSMPTEFrameRate(static_cast<SMPTEFrameRate>(state.smpteRate));
        setSyncMode(static_cast<SyncMode>(state.syncMode));

        audioPlayer_.setGainDecibels(state.audioGainDb);
        audioPlayer_.setMuted(state.isAudioMuted);
        audioPlayer_.setDimmed(state.isAudioDimmed);

        if (state.isFloatingWindowVisible)
        {
            showFloatingWindow(true);
            if (floatingWindow_)
            {
                floatingWindow_->restoreWindowGeometry(state.floatingWindowGeometry);
                floatingWindow_->setAlwaysOnTopActive(state.isAlwaysOnTop);
                floatingWindow_->setBorderlessActive(state.isBorderless);
                floatingWindow_->setClickThroughActive(state.isClickThrough);
            }
        }

        if (!state.markersJson.isVoid())
        {
            markerModel_.importFromJson(state.markersJson);
        }

        if (state.videoFilePath.isNotEmpty())
        {
            juce::File vf(state.videoFilePath);
            if (vf.existsAsFile())
            {
                loadVideoFile(vf);
            }
        }
    }
}

void MScoreVideoSyncProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    SerializedPluginState state;
    state.videoFilePath = lastLoadedVideoPath_;
    state.videoOffsetSeconds = videoOffsetSeconds_.load(std::memory_order_relaxed);
    state.smpteStartTimeSeconds = syncEngine_.getSmpteStartTime();
    state.movieStartOffsetSeconds = syncEngine_.getMovieStartOffset();
    state.displayLatencySeconds = syncEngine_.getDisplayLatencyCompensation();
    state.trimInSeconds = syncEngine_.getTrimInSeconds();
    state.trimOutSeconds = syncEngine_.getTrimOutSeconds();
    state.smpteRate = static_cast<int>(smpteRate_.load(std::memory_order_relaxed));
    state.syncMode = static_cast<int>(syncEngine_.getSyncMode());

    state.audioGainDb = audioPlayer_.getGainDecibels();
    state.isAudioMuted = audioPlayer_.isMuted();
    state.isAudioDimmed = audioPlayer_.isDimmed();

    state.isFloatingWindowVisible = isFloatingWindowVisible();
    if (floatingWindow_)
    {
        state.floatingWindowGeometry = floatingWindow_->getWindowGeometryString();
        state.isAlwaysOnTop = floatingWindow_->isAlwaysOnTopActive();
        state.isBorderless = floatingWindow_->isBorderlessActive();
        state.isClickThrough = floatingWindow_->isClickThroughActive();
    }

    state.markersJson = markerModel_.exportToJson();

    auto vt = ProjectStateSerializer::toValueTree(state);
    std::unique_ptr<juce::XmlElement> xml(vt.createXml());
    if (xml != nullptr)
    {
        copyXmlToBinary(*xml, destData);
    }
}

void MScoreVideoSyncProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
    {
        loadPersistentSettings();
        return;
    }

    std::unique_ptr<juce::XmlElement> xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
    {
        juce::String xmlText = juce::String::createStringFromData(data, sizeInBytes);
        if (xmlText.isNotEmpty()) xml = juce::XmlDocument::parse(xmlText);
    }

    if (xml && xml->hasTagName("MScoreVideoSyncProject"))
    {
        auto vt = juce::ValueTree::fromXml(*xml);
        auto state = ProjectStateSerializer::fromValueTree(vt);

        setVideoOffset(state.videoOffsetSeconds);
        syncEngine_.setSmpteStartTime(state.smpteStartTimeSeconds);
        syncEngine_.setMovieStartOffset(state.movieStartOffsetSeconds);
        syncEngine_.setDisplayLatencyCompensation(state.displayLatencySeconds);
        syncEngine_.setTrimRange(state.trimInSeconds, state.trimOutSeconds);

        setSMPTEFrameRate(static_cast<SMPTEFrameRate>(state.smpteRate));
        setSyncMode(static_cast<SyncMode>(state.syncMode));

        audioPlayer_.setGainDecibels(state.audioGainDb);
        audioPlayer_.setMuted(state.isAudioMuted);
        audioPlayer_.setDimmed(state.isAudioDimmed);

        if (state.isFloatingWindowVisible)
        {
            showFloatingWindow(true);
            if (floatingWindow_)
            {
                floatingWindow_->restoreWindowGeometry(state.floatingWindowGeometry);
                floatingWindow_->setAlwaysOnTopActive(state.isAlwaysOnTop);
                floatingWindow_->setBorderlessActive(state.isBorderless);
                floatingWindow_->setClickThroughActive(state.isClickThrough);
            }
        }

        if (!state.markersJson.isVoid())
        {
            markerModel_.importFromJson(state.markersJson);
        }

        if (state.videoFilePath.isNotEmpty())
        {
            juce::File vf(state.videoFilePath);
            if (vf.existsAsFile())
            {
                loadVideoFile(vf);
            }
        }
    }
    else
    {
        loadPersistentSettings();
    }
}

} // namespace MScoreVideoSync

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MScoreVideoSync::MScoreVideoSyncProcessor();
}
