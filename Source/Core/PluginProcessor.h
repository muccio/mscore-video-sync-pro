#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CommonTypes.h"
#include "TempoMapTracker.h"
#include "../Video/FFmpegDecoder.h"

namespace MScoreVideoSync
{

class MScoreVideoSyncProcessor : public juce::AudioProcessor
{
public:
    MScoreVideoSyncProcessor();
    ~MScoreVideoSyncProcessor() override;

    // AudioProcessor lifecycle
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages) override;

    // GUI Editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // Plugin Info
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // Programs
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // State persistence
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Video Engine Access
    FFmpegDecoder& getDecoder() noexcept { return decoder_; }
    TempoMapTracker& getTempoTracker() noexcept { return tempoTracker_; }

    // Video offset control
    void setVideoOffset(double offsetSeconds);
    double getVideoOffset() const noexcept;

    // SMPTE Frame Rate setting
    void setSMPTEFrameRate(SMPTEFrameRate rate);
    SMPTEFrameRate getSMPTEFrameRate() const noexcept;

    // Sync Mode (DAWPlayHead, Auto, AudioStream, MIDIClock, Manual)
    void setSyncMode(SyncMode mode);
    SyncMode getSyncMode() const noexcept;

    // Locate transport / score request
    void requestHostLocate(double timeSeconds);

    // File loading
    bool loadVideoFile(const juce::File& file);
    void closeVideoFile();
    juce::String getLoadedVideoPath() const;

    // Persistent storage helpers (Application Support fallback)
    void savePersistentSettings();
    void loadPersistentSettings();

private:
    FFmpegDecoder decoder_;
    TempoMapTracker tempoTracker_;

    juce::AudioParameterFloat* paramOffset_{ nullptr };
    juce::AudioParameterChoice* paramSmpteRate_{ nullptr };
    juce::AudioParameterChoice* paramSyncMode_{ nullptr };

    std::atomic<SMPTEFrameRate> smpteRate_{ SMPTEFrameRate::FPS_24 };
    std::atomic<double> videoOffsetSeconds_{ 0.0 };
    std::atomic<double> pendingMidiLocateTime_{ -1.0 };

    juce::String lastLoadedVideoPath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MScoreVideoSyncProcessor)
};

} // namespace MScoreVideoSync
