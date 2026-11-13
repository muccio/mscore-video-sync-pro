#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CommonTypes.h"
#include "ProjectState.h"
#include "../Sync/TransportSyncEngine.h"
#include "../Sync/SharedMemoryIPC.h"
#include "../Sync/NetworkBridge.h"
#include "../Video/VideoDecodeEngine.h"
#include "../Audio/EmbeddedAudioPlayer.h"
#include "../Audio/TwoPopGenerator.h"
#include "../Cues/CueMarkerModel.h"
#include "../UI/Window/FloatingVideoWindow.h"

namespace MScoreVideoSync
{

class MScoreVideoSyncProcessor : public juce::AudioProcessor,
                                 public NetworkBridge::CommandListener
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

    // Subsystem accessors
    VideoDecodeEngine& getDecoder() noexcept { return decoder_; }
    TransportSyncEngine& getSyncEngine() noexcept { return syncEngine_; }
    EmbeddedAudioPlayer& getAudioPlayer() noexcept { return audioPlayer_; }
    TwoPopGenerator& getTwoPopGenerator() noexcept { return twoPopGen_; }
    CueMarkerModel& getMarkerModel() noexcept { return markerModel_; }
    SharedMemoryIPC& getIPC() noexcept { return ipc_; }
    NetworkBridge& getNetworkBridge() noexcept { return networkBridge_; }

    // Persistent Floating Window (lives in processor to survive MuseScore 4 editor destruction)
    void showFloatingWindow(bool show);
    bool isFloatingWindowVisible() const;
    FloatingVideoWindow* getFloatingWindow() noexcept { return floatingWindow_.get(); }

    // File loading
    bool loadVideoFile(const juce::File& file);
    void closeVideoFile();
    juce::String getLoadedVideoPath() const;

    // Settings & Controls
    void setVideoOffset(double offsetSeconds);
    double getVideoOffset() const noexcept;

    void setSMPTEFrameRate(SMPTEFrameRate rate);
    SMPTEFrameRate getSMPTEFrameRate() const noexcept;

    void setSyncMode(SyncMode mode);
    SyncMode getSyncMode() const noexcept;

    // Locate request
    void requestHostLocate(double timeSeconds);

    // NetworkBridge::CommandListener overrides
    void onRemotePlay() override;
    void onRemotePause() override;
    void onRemoteStop() override;
    void onRemoteLocate(double timeSeconds) override;

    void savePersistentSettings();
    void loadPersistentSettings();

private:
    template <typename FloatType>
    void processBlockInternal(juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midiMessages);

    // Engines
    VideoDecodeEngine decoder_;
    TransportSyncEngine syncEngine_;
    EmbeddedAudioPlayer audioPlayer_;
    TwoPopGenerator twoPopGen_;
    CueMarkerModel markerModel_;
    SharedMemoryIPC ipc_;
    NetworkBridge networkBridge_;

    // Persistent Window
    std::unique_ptr<FloatingVideoWindow> floatingWindow_;

    // Parameters
    juce::AudioParameterFloat* paramOffset_{ nullptr };
    juce::AudioParameterChoice* paramSmpteRate_{ nullptr };
    juce::AudioParameterChoice* paramSyncMode_{ nullptr };

    std::atomic<SMPTEFrameRate> smpteRate_{ SMPTEFrameRate::FPS_24 };
    std::atomic<double> videoOffsetSeconds_{ 0.0 };
    std::atomic<double> pendingMidiLocateTime_{ -1.0 };
    std::atomic<double> lastProcessedPts_{ -1.0 };

    juce::String lastLoadedVideoPath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MScoreVideoSyncProcessor)
};

} // namespace MScoreVideoSync
