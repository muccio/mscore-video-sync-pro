#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/PluginProcessor.h"
#include "Viewport/VideoViewport.h"
#include "Timeline/TimelineComponent.h"
#include "Inspector/MarkerInspector.h"
#include "Theme/Vep8Theme.h"

namespace MScoreVideoSync
{

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::Timer,
                     public juce::FileDragAndDropTarget,
                     public TimelineComponent::Listener,
                     public MarkerInspector::Listener,
                     public VideoDecodeEngine::Listener
{
public:
    PluginEditor(MScoreVideoSyncProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // 60Hz UI Timer
    void timerCallback() override;

    // Keyboard Shortcuts
    bool keyPressed(const juce::KeyPress& key) override;

    // File Drag and Drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // TimelineComponent::Listener
    void timelineScrubbed(double targetPts) override;
    void markerSelected(int markerId) override;

    // MarkerInspector::Listener
    void locateToMarker(double timeSeconds) override;

    // VideoDecodeEngine::Listener
    void videoLoaded(const VideoMetadata& metadata) override;
    void videoLoadFailed(const juce::String& errorMessage) override;

private:
    void openFilePicker();
    void closeCurrentVideo();
    void toggleInspector();
    void toggleFloatingWindow();

    MScoreVideoSyncProcessor& processor_;
    Vep8LookAndFeel vep8LookAndFeel_;

    // Main central viewport
    VideoViewport videoViewport_;

    // Bottom Multi-Lane Timeline
    TimelineComponent timelineComponent_;

    // Collapsible Marker Inspector Drawer
    MarkerInspector markerInspector_;

    // Top Control Bar Components
    juce::TextButton btnLoadVideo_{ "Load Video..." };
    juce::TextButton btnCloseVideo_{ "Clear" };
    juce::Label lblVideoFileName_{ "lblVideoFileName", "No Video" };

    juce::ComboBox cmbSyncMode_;
    juce::ComboBox cmbSmpteRate_;
    juce::ComboBox cmbAspectRatio_;

    juce::Label lblOffset_{ "lblOffset", "Offset:" };
    juce::Slider sliderOffset_;
    juce::TextButton btnResetOffset_{ "0s" };

    juce::Label lblLatency_{ "lblLatency", "Latency:" };
    juce::Slider sliderLatency_;

    juce::TextButton btnToggleOsd_{ "OSD" };
    juce::TextButton btnToggleCues_{ "Cues" };
    juce::TextButton btnToggleInspector_{ "Markers" };
    juce::TextButton btnFloatWindow_{ "Float" };

    // Bottom Transport & Audio Strip
    juce::TextButton btnRewind_{ "|<" };
    juce::TextButton btnStepBackSec_{ "-1s" };
    juce::TextButton btnStepBackFrame_{ "-1f" };
    juce::TextButton btnPlayPause_{ "Play" };
    juce::TextButton btnStepFwdFrame_{ "+1f" };
    juce::TextButton btnStepFwdSec_{ "+1s" };
    juce::TextButton btnAddMarker_{ "+ Cue (M)" };

    juce::Label lblVolume_{ "lblVol", "Vol:" };
    juce::Slider sliderVolume_;
    juce::TextButton btnMute_{ "M" };
    juce::TextButton btnDim_{ "Dim" };

    juce::Label lblZoom_{ "lblZoom", "Zoom:" };
    juce::Slider sliderZoom_;

    float currentMeterL_{ 0.0f };
    float currentMeterR_{ 0.0f };

    bool showInspector_{ false };
    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace MScoreVideoSync
