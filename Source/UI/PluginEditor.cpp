#include "PluginEditor.h"

namespace MScoreVideoSync
{

PluginEditor::PluginEditor(MScoreVideoSyncProcessor& proc)
    : AudioProcessorEditor(&proc),
      processor_(proc),
      videoViewport_(proc.getDecoder(), proc.getMarkerModel()),
      timelineComponent_(proc.getSyncEngine(), proc.getDecoder(), proc.getMarkerModel()),
      markerInspector_(proc.getMarkerModel(), proc.getSMPTEFrameRate())
{
    setLookAndFeel(&vep8LookAndFeel_);

    addAndMakeVisible(videoViewport_);
    addAndMakeVisible(timelineComponent_);
    timelineComponent_.addListener(this);

    addChildComponent(markerInspector_);
    markerInspector_.addListener(this);

    processor_.getDecoder().addListener(this);

    // =========================================================================
    // Top Control Bar
    // =========================================================================
    addAndMakeVisible(btnLoadVideo_);
    btnLoadVideo_.onClick = [this] { openFilePicker(); };

    addAndMakeVisible(btnCloseVideo_);
    btnCloseVideo_.onClick = [this] { closeCurrentVideo(); };

    addAndMakeVisible(lblVideoFileName_);
    lblVideoFileName_.setFont(Vep8Theme::getUiFont(12.0f, true));
    lblVideoFileName_.setColour(juce::Label::textColourId, Vep8Theme::cyan);

    // Sync Mode
    for (int i = 0; i < static_cast<int>(SyncMode::Count); ++i)
        cmbSyncMode_.addItem(getSyncModeLabel(static_cast<SyncMode>(i)), i + 1);
    cmbSyncMode_.setSelectedId(static_cast<int>(processor_.getSyncMode()) + 1, juce::dontSendNotification);
    cmbSyncMode_.onChange = [this] {
        processor_.setSyncMode(static_cast<SyncMode>(cmbSyncMode_.getSelectedId() - 1));
    };
    addAndMakeVisible(cmbSyncMode_);

    // SMPTE Rate
    for (int i = 0; i < static_cast<int>(SMPTEFrameRate::Count); ++i)
        cmbSmpteRate_.addItem(getFrameRateLabel(static_cast<SMPTEFrameRate>(i)), i + 1);
    cmbSmpteRate_.setSelectedId(static_cast<int>(processor_.getSMPTEFrameRate()) + 1, juce::dontSendNotification);
    cmbSmpteRate_.onChange = [this] {
        auto rate = static_cast<SMPTEFrameRate>(cmbSmpteRate_.getSelectedId() - 1);
        processor_.setSMPTEFrameRate(rate);
        markerInspector_.setSmpteFrameRate(rate);
    };
    addAndMakeVisible(cmbSmpteRate_);

    // Aspect Ratio
    cmbAspectRatio_.addItem("Native", 1);
    cmbAspectRatio_.addItem("16:9", 2);
    cmbAspectRatio_.addItem("2.39:1", 3);
    cmbAspectRatio_.addItem("1.85:1", 4);
    cmbAspectRatio_.addItem("4:3", 5);
    cmbAspectRatio_.addItem("Fill", 6);
    cmbAspectRatio_.setSelectedId(1, juce::dontSendNotification);
    cmbAspectRatio_.onChange = [this] {
        videoViewport_.setAspectRatioMode(static_cast<AspectRatioMode>(cmbAspectRatio_.getSelectedId() - 1));
    };
    addAndMakeVisible(cmbAspectRatio_);

    // Offset Controls
    lblOffset_.setFont(Vep8Theme::getUiFont(11.0f));
    addAndMakeVisible(lblOffset_);

    sliderOffset_.setRange(-30.0, 30.0, 0.01);
    sliderOffset_.setValue(processor_.getVideoOffset(), juce::dontSendNotification);
    sliderOffset_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    sliderOffset_.onValueChange = [this] {
        processor_.setVideoOffset(sliderOffset_.getValue());
    };
    addAndMakeVisible(sliderOffset_);

    addAndMakeVisible(btnResetOffset_);
    btnResetOffset_.onClick = [this] {
        sliderOffset_.setValue(0.0);
    };

    // Latency Calibration
    lblLatency_.setFont(Vep8Theme::getUiFont(11.0f));
    addAndMakeVisible(lblLatency_);

    sliderLatency_.setRange(-500.0, 500.0, 1.0);
    sliderLatency_.setValue(processor_.getSyncEngine().getDisplayLatencyCompensation() * 1000.0, juce::dontSendNotification);
    sliderLatency_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 20);
    sliderLatency_.setTextValueSuffix("ms");
    sliderLatency_.onValueChange = [this] {
        processor_.getSyncEngine().setDisplayLatencyCompensation(sliderLatency_.getValue() * 0.001);
    };
    addAndMakeVisible(sliderLatency_);

    // Toggles
    btnToggleOsd_.setClickingTogglesState(true);
    btnToggleOsd_.setToggleState(videoViewport_.getOverlay().isOsdVisible(), juce::dontSendNotification);
    btnToggleOsd_.onClick = [this] {
        videoViewport_.getOverlay().setOsdVisible(btnToggleOsd_.getToggleState());
    };
    addAndMakeVisible(btnToggleOsd_);

    btnToggleCues_.setClickingTogglesState(true);
    btnToggleCues_.setToggleState(videoViewport_.getOverlay().isStreamersVisible(), juce::dontSendNotification);
    btnToggleCues_.onClick = [this] {
        bool show = btnToggleCues_.getToggleState();
        videoViewport_.getOverlay().setStreamersVisible(show);
        videoViewport_.getOverlay().setPunchesVisible(show);
    };
    addAndMakeVisible(btnToggleCues_);

    btnToggleInspector_.setClickingTogglesState(true);
    btnToggleInspector_.setToggleState(false, juce::dontSendNotification);
    btnToggleInspector_.onClick = [this] { toggleInspector(); };
    addAndMakeVisible(btnToggleInspector_);

    addAndMakeVisible(btnFloatWindow_);
    btnFloatWindow_.setToggleState(processor_.isFloatingWindowVisible(), juce::dontSendNotification);
    btnFloatWindow_.onClick = [this] { toggleFloatingWindow(); };

    // =========================================================================
    // Bottom Transport & Audio Strip
    // =========================================================================
    addAndMakeVisible(btnRewind_);
    btnRewind_.onClick = [this] {
        processor_.getSyncEngine().freeScrubToTime(0.0);
    };

    addAndMakeVisible(btnStepBackSec_);
    btnStepBackSec_.onClick = [this] {
        processor_.getSyncEngine().stepSeconds(-1.0);
    };

    addAndMakeVisible(btnStepBackFrame_);
    btnStepBackFrame_.onClick = [this] {
        processor_.getSyncEngine().stepFrames(-1, processor_.getSMPTEFrameRate());
    };

    addAndMakeVisible(btnPlayPause_);
    btnPlayPause_.onClick = [this] {
        bool playing = processor_.getSyncEngine().isLocalPlaying();
        processor_.getSyncEngine().setLocalPlaying(!playing);
    };

    addAndMakeVisible(btnStepFwdFrame_);
    btnStepFwdFrame_.onClick = [this] {
        processor_.getSyncEngine().stepFrames(1, processor_.getSMPTEFrameRate());
    };

    addAndMakeVisible(btnStepFwdSec_);
    btnStepFwdSec_.onClick = [this] {
        processor_.getSyncEngine().stepSeconds(1.0);
    };

    addAndMakeVisible(btnAddMarker_);
    btnAddMarker_.onClick = [this] {
        auto snap = processor_.getSyncEngine().getSnapshot();
        processor_.getMarkerModel().addQuickMarker(snap.timeInSeconds, snap.ppqPosition, snap.bar, snap.beat);
    };

    // Audio Controls
    lblVolume_.setFont(Vep8Theme::getUiFont(11.0f));
    addAndMakeVisible(lblVolume_);

    sliderVolume_.setRange(-60.0, 12.0, 0.5);
    sliderVolume_.setValue(processor_.getAudioPlayer().getGainDecibels(), juce::dontSendNotification);
    sliderVolume_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    sliderVolume_.onValueChange = [this] {
        processor_.getAudioPlayer().setGainDecibels(static_cast<float>(sliderVolume_.getValue()));
    };
    addAndMakeVisible(sliderVolume_);

    btnMute_.setClickingTogglesState(true);
    btnMute_.setToggleState(processor_.getAudioPlayer().isMuted(), juce::dontSendNotification);
    btnMute_.onClick = [this] {
        processor_.getAudioPlayer().setMuted(btnMute_.getToggleState());
    };
    addAndMakeVisible(btnMute_);

    btnDim_.setClickingTogglesState(true);
    btnDim_.setToggleState(processor_.getAudioPlayer().isDimmed(), juce::dontSendNotification);
    btnDim_.onClick = [this] {
        processor_.getAudioPlayer().setDimmed(btnDim_.getToggleState());
    };
    addAndMakeVisible(btnDim_);

    lblZoom_.setFont(Vep8Theme::getUiFont(11.0f));
    addAndMakeVisible(lblZoom_);

    sliderZoom_.setRange(0.25, 10.0, 0.05);
    sliderZoom_.setValue(timelineComponent_.getZoomFactor(), juce::dontSendNotification);
    sliderZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sliderZoom_.onValueChange = [this] {
        timelineComponent_.setZoomFactor(sliderZoom_.getValue());
    };
    addAndMakeVisible(sliderZoom_);

    // Window defaults
    setSize(1080, 680);
    setResizable(true, true);
    setResizeLimits(720, 480, 3840, 2160);

    // Initial check for loaded video
    juce::String currentPath = processor_.getLoadedVideoPath();
    if (currentPath.isNotEmpty())
    {
        juce::File vf(currentPath);
        lblVideoFileName_.setText(vf.getFileName(), juce::dontSendNotification);
    }

    startTimerHz(60);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    processor_.getDecoder().removeListener(this);
}

void PluginEditor::timerCallback()
{
    // Watchdog
    double nowWallSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool watchdogStopped = processor_.getSyncEngine().checkWatchdog(nowWallSec);

    auto snapshot = processor_.getSyncEngine().getSnapshot();
    auto smpteRate = processor_.getSMPTEFrameRate();

    if (watchdogStopped)
    {
        const double pts = snapshot.getTargetVideoPts();
        processor_.getDecoder().setPlaybackState(false, pts);
        processor_.getAudioPlayer().setPlaybackState(false, pts);
    }

    videoViewport_.updateSnapshot(snapshot, smpteRate);
    timelineComponent_.updateSnapshot(snapshot, smpteRate);

    // Update play button label
    btnPlayPause_.setButtonText(snapshot.isPlaying ? "Pause" : "Play");

    // Update Audio Peak Meters
    currentMeterL_ = processor_.getAudioPlayer().getPeakLeft();
    currentMeterR_ = processor_.getAudioPlayer().getPeakRight();

    repaint();
}

bool PluginEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        btnPlayPause_.triggerClick();
        return true;
    }
    if (key.getKeyCode() == 'M' || key.getKeyCode() == 'm')
    {
        btnAddMarker_.triggerClick();
        return true;
    }
    if (key == juce::KeyPress::leftKey)
    {
        btnStepBackFrame_.triggerClick();
        return true;
    }
    if (key == juce::KeyPress::rightKey)
    {
        btnStepFwdFrame_.triggerClick();
        return true;
    }
    return false;
}

bool PluginEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (files.size() != 1) return false;
    juce::String ext = juce::File(files[0]).getFileExtension().toLowerCase();
    return ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".m4v"
        || ext == ".avi" || ext == ".webm" || ext == ".mts" || ext == ".m2ts"
        || ext == ".ts" || ext == ".mpg" || ext == ".mpeg" || ext == ".prores";
}

void PluginEditor::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    if (!files.isEmpty())
    {
        juce::File file(files[0]);
        processor_.loadVideoFile(file);
    }
}

void PluginEditor::openFilePicker()
{
    auto startDir = juce::File::getSpecialLocation(juce::File::userMoviesDirectory);
    if (!startDir.exists())
        startDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    fileChooser_ = std::make_unique<juce::FileChooser>("Select Video Score File",
                                                        startDir,
                                                        "*.mp4;*.mov;*.mkv;*.m4v;*.avi;*.webm;*.mts;*.m2ts;*.ts;*.mpg;*.mpeg");

    juce::Component::SafePointer<PluginEditor> safeThis(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis](const juce::FileChooser& fc) {
                                  if (safeThis == nullptr)
                                      return;
                                  auto file = fc.getResult();
                                  if (file.existsAsFile())
                                  {
                                      safeThis->processor_.loadVideoFile(file);
                                  }
                              });
}

void PluginEditor::closeCurrentVideo()
{
    processor_.closeVideoFile();
    lblVideoFileName_.setText("No Video", juce::dontSendNotification);
}

void PluginEditor::toggleInspector()
{
    showInspector_ = btnToggleInspector_.getToggleState();
    markerInspector_.setVisible(showInspector_);
    resized();
}

void PluginEditor::toggleFloatingWindow()
{
    bool isVisible = processor_.isFloatingWindowVisible();
    processor_.showFloatingWindow(!isVisible);
    btnFloatWindow_.setToggleState(!isVisible, juce::dontSendNotification);
}

void PluginEditor::timelineScrubbed(double targetPts)
{
    processor_.getDecoder().requestSeek(targetPts);
    processor_.getAudioPlayer().requestSeek(targetPts);
}

void PluginEditor::markerSelected(int markerId)
{
    CueMarker m;
    if (processor_.getMarkerModel().getMarkerById(markerId, m))
    {
        processor_.getSyncEngine().freeScrubToTime(m.timeSeconds);
        processor_.getDecoder().requestSeek(m.timeSeconds);
    }
}

void PluginEditor::locateToMarker(double timeSeconds)
{
    processor_.getSyncEngine().freeScrubToTime(timeSeconds);
    processor_.getDecoder().requestSeek(timeSeconds);
}

void PluginEditor::videoLoaded(const VideoMetadata& metadata)
{
    juce::File vf(metadata.filePath);
    lblVideoFileName_.setText(vf.getFileName(), juce::dontSendNotification);
    cmbSmpteRate_.setSelectedId(static_cast<int>(processor_.getSMPTEFrameRate()) + 1, juce::dontSendNotification);
    resized();
}

void PluginEditor::videoLoadFailed(const juce::String& errorMessage)
{
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                           "Video Load Failed",
                                           errorMessage,
                                           "OK");
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(Vep8Theme::bgMain);

    // Audio Stereo Peak Meter in bottom strip
    int meterX = getWidth() - (showInspector_ ? 370 : 10) - 50;
    int meterY = getHeight() - 30;
    int meterW = 38;
    int meterH = 10;

    g.setColour(Vep8Theme::bgInput);
    g.fillRect(meterX, meterY, meterW, meterH);

    float lNorm = juce::jlimit(0.0f, 1.0f, currentMeterL_);
    float rNorm = juce::jlimit(0.0f, 1.0f, currentMeterR_);

    g.setColour(Vep8Theme::green);
    g.fillRect(meterX, meterY, static_cast<int>(static_cast<float>(meterW) * lNorm), 4);
    g.fillRect(meterX, meterY + 5, static_cast<int>(static_cast<float>(meterW) * rNorm), 4);
}

void PluginEditor::resized()
{
    auto bounds = getLocalBounds();

    // =========================================================================
    // 1. Top Control Header (2 compact rows: Height 64)
    // =========================================================================
    auto headerArea = bounds.removeFromTop(64);
    auto topRow1 = headerArea.removeFromTop(32).reduced(6, 3);
    auto topRow2 = headerArea.reduced(6, 3);

    btnLoadVideo_.setBounds(topRow1.removeFromLeft(105).reduced(1, 0));
    btnCloseVideo_.setBounds(topRow1.removeFromLeft(46).reduced(1, 0));

    btnFloatWindow_.setBounds(topRow1.removeFromRight(68).reduced(2, 0));
    btnToggleInspector_.setBounds(topRow1.removeFromRight(72).reduced(2, 0));
    btnToggleCues_.setBounds(topRow1.removeFromRight(54).reduced(2, 0));
    btnToggleOsd_.setBounds(topRow1.removeFromRight(50).reduced(2, 0));
    lblVideoFileName_.setBounds(topRow1.reduced(6, 0));

    cmbSyncMode_.setBounds(topRow2.removeFromLeft(155).reduced(2, 0));
    cmbSmpteRate_.setBounds(topRow2.removeFromLeft(102).reduced(2, 0));
    cmbAspectRatio_.setBounds(topRow2.removeFromLeft(78).reduced(2, 0));

    lblOffset_.setBounds(topRow2.removeFromLeft(46));
    sliderOffset_.setBounds(topRow2.removeFromLeft(110));
    btnResetOffset_.setBounds(topRow2.removeFromLeft(30).reduced(1, 0));

    lblLatency_.setBounds(topRow2.removeFromLeft(54));
    sliderLatency_.setBounds(topRow2.removeFromLeft(105));

    // =========================================================================
    // 2. Bottom Timeline & Transport Area (Height: 160)
    // =========================================================================
    auto bottomArea = bounds.removeFromBottom(160);
    auto transportStrip = bottomArea.removeFromBottom(34).reduced(6, 3);

    // Transport buttons
    btnRewind_.setBounds(transportStrip.removeFromLeft(32).reduced(1, 0));
    btnStepBackSec_.setBounds(transportStrip.removeFromLeft(36).reduced(1, 0));
    btnStepBackFrame_.setBounds(transportStrip.removeFromLeft(36).reduced(1, 0));
    btnPlayPause_.setBounds(transportStrip.removeFromLeft(72).reduced(1, 0));
    btnStepFwdFrame_.setBounds(transportStrip.removeFromLeft(36).reduced(1, 0));
    btnStepFwdSec_.setBounds(transportStrip.removeFromLeft(36).reduced(1, 0));
    btnAddMarker_.setBounds(transportStrip.removeFromLeft(88).reduced(2, 0));

    // Audio controls
    lblVolume_.setBounds(transportStrip.removeFromLeft(32));
    sliderVolume_.setBounds(transportStrip.removeFromLeft(95));
    btnMute_.setBounds(transportStrip.removeFromLeft(30).reduced(1, 0));
    btnDim_.setBounds(transportStrip.removeFromLeft(40).reduced(1, 0));

    // Timeline Zoom
    lblZoom_.setBounds(transportStrip.removeFromLeft(40));
    sliderZoom_.setBounds(transportStrip.removeFromLeft(85));

    // Multi-lane Timeline
    timelineComponent_.setBounds(bottomArea);

    // =========================================================================
    // 3. Central Video Viewport + Marker Inspector Drawer
    // =========================================================================
    if (showInspector_)
    {
        auto rightDrawer = bounds.removeFromRight(340);
        markerInspector_.setBounds(rightDrawer);
    }

    videoViewport_.setBounds(bounds);
}

} // namespace MScoreVideoSync
