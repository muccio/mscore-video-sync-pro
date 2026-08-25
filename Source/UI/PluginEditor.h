#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/PluginProcessor.h"
#include "VideoViewport.h"

namespace MScoreVideoSync
{

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::Timer,
                     public juce::FileDragAndDropTarget,
                     public FFmpegDecoder::Listener
{
public:
    PluginEditor(MScoreVideoSyncProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Timer for 60Hz UI and HUD refresh
    void timerCallback() override;

    // Keyboard Shortcuts (Spacebar for play/pause)
    bool keyPressed(const juce::KeyPress& key) override;

    // File Drag and Drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // FFmpegDecoder::Listener overrides
    void videoLoaded(const VideoMetadata& metadata) override;
    void videoLoadFailed(const juce::String& errorMessage) override;

private:
    void openFilePicker();
    void closeCurrentVideo();
    void setupUI();

    MScoreVideoSyncProcessor& processor_;

    // Main Video Viewport
    VideoViewport videoViewport_;

    // Top Header Controls
    juce::Label lblTitle_{ "lblTitle", "MScore Video Sync Pro" };
    juce::TextButton btnLoadVideo_{ "📂 Load Video..." };
    juce::TextButton btnCloseVideo_{ "✕ Close" };

    juce::Label lblOffset_{ "lblOffset", "Offset:" };
    juce::Slider sliderOffset_;
    juce::TextButton btnResetOffset_{ "0s" };

    juce::Label lblSmpte_{ "lblSmpte", "SMPTE:" };
    juce::ComboBox cmbSmpteRate_;

    juce::ComboBox cmbSyncMode_;
    juce::ToggleButton btnToggleHUD_{ "HUD" };
    juce::ToggleButton btnToggleOpenGL_{ "OpenGL HW" };

    // Bottom Transport / Scrubber Bar
    juce::TextButton btnRewind_{ "⏮" };
    juce::TextButton btnPlayPause_{ "▶ Play" };
    juce::Label lblHostStatus_;
    juce::Slider sliderTimeline_;
    juce::Label lblTimelinePos_;

    bool isManualPlaying_{ false };
    double manualPlayStartWallTime_{ 0.0 };
    double manualPlayStartPts_{ 0.0 };

    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace MScoreVideoSync
