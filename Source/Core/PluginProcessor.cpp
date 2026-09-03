#include "PluginProcessor.h"
#include "../UI/PluginEditor.h"

namespace MScoreVideoSync
{

MScoreVideoSyncProcessor::MScoreVideoSyncProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Register parameters for VST3 automation and dirty-state tracking
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
        static_cast<int>(SyncMode::DAWPlayHead)));

    // Load persistent settings fallback
    loadPersistentSettings();
}

MScoreVideoSyncProcessor::~MScoreVideoSyncProcessor()
{
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

void MScoreVideoSyncProcessor::prepareToPlay(double /*sampleRate*/, int /*samplesPerBlock*/)
{
    tempoTracker_.resetInternalClock();
    if (!decoder_.isThreadRunning())
    {
        decoder_.startThread(juce::Thread::Priority::high);
    }
}

void MScoreVideoSyncProcessor::releaseResources()
{
    // Note: Do NOT stop decoder_ thread here.
    // Video decoding thread must remain active while audio engine is paused or idle
    // so scrubbing, note selection, and UI rendering continue to work.
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

void MScoreVideoSyncProcessor::requestHostLocate(double timeSeconds)
{
    pendingMidiLocateTime_.store(timeSeconds, std::memory_order_release);
    decoder_.requestSeek(tempoTracker_.calculateTargetPts(timeSeconds));
}

void MScoreVideoSyncProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Real-Time Audio Thread Safety: No heap allocations, no locks, no Obj-C ARC overhead
    tempoTracker_.processAudioBlock(getPlayHead(), buffer, midiMessages, getSampleRate());

    // Send MIDI Locate / SPP / MMC if requested
    double locateTime = pendingMidiLocateTime_.exchange(-1.0, std::memory_order_acq_rel);
    if (locateTime >= 0.0)
    {
        double bpm = tempoTracker_.getScoreTempoBpm();
        double ppq = (locateTime * bpm) / 60.0;
        int midiBeats = static_cast<int>(std::round(ppq * 4.0));
        int lsb = midiBeats & 0x7F;
        int msb = (midiBeats >> 7) & 0x7F;
        midiMessages.addEvent(juce::MidiMessage(0xF2, lsb, msb), 0);

        int hr = static_cast<int>(locateTime / 3600.0) % 24;
        int mn = static_cast<int>(locateTime / 60.0) % 60;
        int sc = static_cast<int>(locateTime) % 60;
        double frac = locateTime - std::floor(locateTime);
        int fr = static_cast<int>(frac * 24.0);
        int ff = static_cast<int>((frac * 24.0 - fr) * 100.0);

        uint8_t mmcData[] = {
            0xF0, 0x7F, 0x7F, 0x06, 0x44, 0x06, 0x01,
            static_cast<uint8_t>(hr),
            static_cast<uint8_t>(mn),
            static_cast<uint8_t>(sc),
            static_cast<uint8_t>(fr),
            static_cast<uint8_t>(ff),
            0xF7
        };
        midiMessages.addEvent(juce::MidiMessage(mmcData, sizeof(mmcData)), 0);
    }

    auto snapshot = tempoTracker_.getSnapshot();
    decoder_.setPlaybackState(snapshot.isPlaying, snapshot.getTargetVideoPts());
}

void MScoreVideoSyncProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    tempoTracker_.processAudioBlock(getPlayHead(), buffer, midiMessages, getSampleRate());

    double locateTime = pendingMidiLocateTime_.exchange(-1.0, std::memory_order_acq_rel);
    if (locateTime >= 0.0)
    {
        double bpm = tempoTracker_.getScoreTempoBpm();
        double ppq = (locateTime * bpm) / 60.0;
        int midiBeats = static_cast<int>(std::round(ppq * 4.0));
        int lsb = midiBeats & 0x7F;
        int msb = (midiBeats >> 7) & 0x7F;
        midiMessages.addEvent(juce::MidiMessage(0xF2, lsb, msb), 0);
    }

    auto snapshot = tempoTracker_.getSnapshot();
    decoder_.setPlaybackState(snapshot.isPlaying, snapshot.getTargetVideoPts());
}

bool MScoreVideoSyncProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* MScoreVideoSyncProcessor::createEditor()
{
    return new PluginEditor(*this);
}

static juce::File getSettingsFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("MScoreVideoSyncPro");
    if (!dir.exists())
    {
        dir.createDirectory();
    }
    return dir.getChildFile("settings.xml");
}

void MScoreVideoSyncProcessor::savePersistentSettings()
{
    juce::XmlElement xml("MScoreVideoSyncSettings");
    xml.setAttribute("lastVideoPath", lastLoadedVideoPath_);
    xml.setAttribute("lastOffset", videoOffsetSeconds_.load(std::memory_order_relaxed));
    xml.setAttribute("lastSmpteRate", static_cast<int>(smpteRate_.load(std::memory_order_relaxed)));
    xml.setAttribute("lastSyncMode", static_cast<int>(getSyncMode()));

    auto file = getSettingsFile();
    xml.writeTo(file);
}

void MScoreVideoSyncProcessor::loadPersistentSettings()
{
    auto file = getSettingsFile();
    if (file.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(file);
        if (xml && xml->hasTagName("MScoreVideoSyncSettings"))
        {
            double offset = xml->getDoubleAttribute("lastOffset", 0.0);
            videoOffsetSeconds_.store(offset, std::memory_order_release);
            tempoTracker_.setVideoOffsetSeconds(offset);

            int smpte = xml->getIntAttribute("lastSmpteRate", static_cast<int>(SMPTEFrameRate::FPS_24));
            smpteRate_.store(static_cast<SMPTEFrameRate>(smpte), std::memory_order_release);

            int mode = xml->getIntAttribute("lastSyncMode", static_cast<int>(SyncMode::DAWPlayHead));
            tempoTracker_.setSyncMode(static_cast<SyncMode>(mode));

            juce::String videoPath = xml->getStringAttribute("lastVideoPath", "");
            if (videoPath.isNotEmpty())
            {
                juce::File vFile(videoPath);
                if (vFile.existsAsFile())
                {
                    if (decoder_.openFile(vFile))
                    {
                        lastLoadedVideoPath_ = vFile.getFullPathName();
                    }
                }
            }
        }
    }
}

void MScoreVideoSyncProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("MScoreVideoSyncState");

    state.setProperty("videoFilePath", lastLoadedVideoPath_, nullptr);
    state.setProperty("videoOffsetSeconds", videoOffsetSeconds_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("smpteRate", static_cast<int>(smpteRate_.load(std::memory_order_relaxed)), nullptr);
    state.setProperty("syncMode", static_cast<int>(getSyncMode()), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
    {
        copyXmlToBinary(*xml, destData);
    }
}

void MScoreVideoSyncProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState;

    if (data != nullptr && sizeInBytes > 0)
    {
        xmlState = getXmlFromBinary(data, sizeInBytes);
        if (xmlState == nullptr)
        {
            // Fallback: parse as plain XML text in case host preserved it as text string
            juce::String xmlText = juce::String::createStringFromData(data, sizeInBytes);
            if (xmlText.isNotEmpty())
            {
                xmlState = juce::XmlDocument::parse(xmlText);
            }
        }
    }

    if (xmlState && xmlState->hasTagName("MScoreVideoSyncState"))
    {
        juce::ValueTree state = juce::ValueTree::fromXml(*xmlState);
        if (state.isValid())
        {
            double offset = static_cast<double>(state.getProperty("videoOffsetSeconds", 0.0));
            setVideoOffset(offset);

            int rateInt = static_cast<int>(state.getProperty("smpteRate", static_cast<int>(SMPTEFrameRate::FPS_24)));
            setSMPTEFrameRate(static_cast<SMPTEFrameRate>(rateInt));

            int modeInt = static_cast<int>(state.getProperty("syncMode", static_cast<int>(SyncMode::DAWPlayHead)));
            setSyncMode(static_cast<SyncMode>(modeInt));

            juce::String videoPath = state.getProperty("videoFilePath", "").toString();
            if (videoPath.isNotEmpty())
            {
                juce::File file(videoPath);
                if (file.existsAsFile())
                {
                    loadVideoFile(file);
                }
            }
        }
    }
    else
    {
        // If host provided empty or unrecognized state, fall back to last session settings
        loadPersistentSettings();
    }
}

void MScoreVideoSyncProcessor::setVideoOffset(double offsetSeconds)
{
    videoOffsetSeconds_.store(offsetSeconds, std::memory_order_release);
    tempoTracker_.setVideoOffsetSeconds(offsetSeconds);
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
    tempoTracker_.setSyncMode(mode);
    if (paramSyncMode_ != nullptr && paramSyncMode_->getIndex() != static_cast<int>(mode))
    {
        *paramSyncMode_ = static_cast<int>(mode);
    }
    savePersistentSettings();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

SyncMode MScoreVideoSyncProcessor::getSyncMode() const noexcept
{
    return tempoTracker_.getSyncMode();
}

bool MScoreVideoSyncProcessor::loadVideoFile(const juce::File& file)
{
    bool success = decoder_.openFile(file);
    if (success)
    {
        lastLoadedVideoPath_ = file.getFullPathName();
        savePersistentSettings();
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }
    return success;
}

void MScoreVideoSyncProcessor::closeVideoFile()
{
    decoder_.closeFile();
    lastLoadedVideoPath_ = {};
    savePersistentSettings();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

juce::String MScoreVideoSyncProcessor::getLoadedVideoPath() const
{
    return lastLoadedVideoPath_;
}

} // namespace MScoreVideoSync

// JUCE Plugin Filter Creator
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MScoreVideoSync::MScoreVideoSyncProcessor();
}
