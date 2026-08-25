#include "PluginEditor.h"

namespace MScoreVideoSync
{

PluginEditor::PluginEditor(MScoreVideoSyncProcessor& proc)
    : AudioProcessorEditor(proc)
    , processor_(proc)
    , videoViewport_(proc.getDecoder())
{
    setupUI();

    addAndMakeVisible(videoViewport_);
    processor_.getDecoder().addListener(this);

    setWantsKeyboardFocus(true);

    // Initial size
    setSize(880, 560);
    setResizable(true, true);
    setResizeLimits(600, 380, 3840, 2160);

    // 60Hz refresh timer for smooth video and SMPTE display
    startTimerHz(60);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    processor_.getDecoder().removeListener(this);
}

bool PluginEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        btnPlayPause_.triggerClick();
        return true;
    }
    return false;
}

void PluginEditor::setupUI()
{
    // Title
    lblTitle_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(0, 220, 255));
    lblTitle_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(lblTitle_);

    // Video File Buttons
    btnLoadVideo_.onClick = [this] { openFilePicker(); };
    btnLoadVideo_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(0, 140, 200));
    addAndMakeVisible(btnLoadVideo_);

    btnCloseVideo_.onClick = [this] { closeCurrentVideo(); };
    btnCloseVideo_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(50, 60, 75));
    addAndMakeVisible(btnCloseVideo_);

    // Video Offset Controls
    lblOffset_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 200, 220));
    lblOffset_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    addAndMakeVisible(lblOffset_);

    sliderOffset_.setRange(-30.0, 30.0, 0.001);
    sliderOffset_.setValue(processor_.getVideoOffset(), juce::dontSendNotification);
    sliderOffset_.setTextValueSuffix(" s");
    sliderOffset_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 18);
    sliderOffset_.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(0, 210, 255));
    sliderOffset_.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(0, 140, 180));
    sliderOffset_.onValueChange = [this] {
        processor_.setVideoOffset(sliderOffset_.getValue());
    };
    addAndMakeVisible(sliderOffset_);

    btnResetOffset_.onClick = [this] {
        sliderOffset_.setValue(0.0);
    };
    btnResetOffset_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 55, 70));
    addAndMakeVisible(btnResetOffset_);

    // SMPTE Rate Selector
    lblSmpte_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 200, 220));
    lblSmpte_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    addAndMakeVisible(lblSmpte_);

    for (int i = 0; i < static_cast<int>(SMPTEFrameRate::Count); ++i)
    {
        cmbSmpteRate_.addItem(getFrameRateLabel(static_cast<SMPTEFrameRate>(i)), i + 1);
    }
    cmbSmpteRate_.setSelectedId(static_cast<int>(processor_.getSMPTEFrameRate()) + 1, juce::dontSendNotification);
    cmbSmpteRate_.onChange = [this] {
        int id = cmbSmpteRate_.getSelectedId();
        if (id > 0)
        {
            processor_.setSMPTEFrameRate(static_cast<SMPTEFrameRate>(id - 1));
        }
    };
    addAndMakeVisible(cmbSmpteRate_);

    // Sync Mode Selector
    for (int i = 0; i < static_cast<int>(SyncMode::Count); ++i)
    {
        cmbSyncMode_.addItem(getSyncModeLabel(static_cast<SyncMode>(i)), i + 1);
    }
    cmbSyncMode_.setSelectedId(static_cast<int>(processor_.getSyncMode()) + 1, juce::dontSendNotification);
    cmbSyncMode_.onChange = [this] {
        int id = cmbSyncMode_.getSelectedId();
        if (id > 0)
        {
            processor_.setSyncMode(static_cast<SyncMode>(id - 1));
        }
    };
    addAndMakeVisible(cmbSyncMode_);

    // Toggles
    btnToggleHUD_.setToggleState(videoViewport_.isHUDVisible(), juce::dontSendNotification);
    btnToggleHUD_.onClick = [this] {
        videoViewport_.setHUDVisible(btnToggleHUD_.getToggleState());
    };
    btnToggleHUD_.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(180, 200, 220));
    addAndMakeVisible(btnToggleHUD_);

    btnToggleOpenGL_.setToggleState(videoViewport_.isOpenGLRendering(), juce::dontSendNotification);
    btnToggleOpenGL_.onClick = [this] {
        videoViewport_.setOpenGLRendering(btnToggleOpenGL_.getToggleState());
    };
    btnToggleOpenGL_.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(180, 200, 220));
    addAndMakeVisible(btnToggleOpenGL_);

    // Bottom Transport Controls
    btnRewind_.onClick = [this] {
        isManualPlaying_ = false;
        btnPlayPause_.setButtonText("▶ Play");
        processor_.getDecoder().requestSeek(0.0);
    };
    btnRewind_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 50, 65));
    addAndMakeVisible(btnRewind_);

    btnPlayPause_.onClick = [this] {
        if (isManualPlaying_)
        {
            isManualPlaying_ = false;
            btnPlayPause_.setButtonText("▶ Play");
            processor_.getDecoder().setPlaybackState(false, processor_.getDecoder().getCurrentDecodedPts());
        }
        else
        {
            isManualPlaying_ = true;
            btnPlayPause_.setButtonText("⏸ Pause");
            manualPlayStartWallTime_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
            manualPlayStartPts_ = processor_.getDecoder().getCurrentDecodedPts();
            processor_.getDecoder().setPlaybackState(true, manualPlayStartPts_);
        }
    };
    btnPlayPause_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(0, 160, 120));
    addAndMakeVisible(btnPlayPause_);

    lblHostStatus_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(140, 160, 180));
    lblHostStatus_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    lblHostStatus_.setText("○ Stopped", juce::dontSendNotification);
    addAndMakeVisible(lblHostStatus_);

    sliderTimeline_.setSliderStyle(juce::Slider::LinearHorizontal);
    sliderTimeline_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sliderTimeline_.setRange(0.0, 100.0, 0.01);
    sliderTimeline_.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(0, 230, 255));
    sliderTimeline_.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(0, 150, 200));
    sliderTimeline_.onValueChange = [this] {
        if (sliderTimeline_.isMouseButtonDown())
        {
            double target = sliderTimeline_.getValue();
            processor_.requestHostLocate(target);
            if (isManualPlaying_)
            {
                manualPlayStartWallTime_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
                manualPlayStartPts_ = target;
            }
        }
    };
    addAndMakeVisible(sliderTimeline_);

    lblTimelinePos_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 220, 240));
    lblTimelinePos_.setFont(juce::FontOptions(11.0f));
    lblTimelinePos_.setText("00:00 / 00:00", juce::dontSendNotification);
    addAndMakeVisible(lblTimelinePos_);
}

void PluginEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Dark background
    g.setColour(juce::Colour::fromRGB(14, 16, 22));
    g.fillRect(bounds);

    // Header bar background
    g.setColour(juce::Colour::fromRGB(22, 26, 36));
    g.fillRect(0, 0, getWidth(), 46);

    g.setColour(juce::Colour::fromRGB(35, 45, 60));
    g.drawHorizontalLine(46, 0.0f, static_cast<float>(getWidth()));

    // Bottom transport background
    g.setColour(juce::Colour::fromRGB(20, 24, 32));
    g.fillRect(0, getHeight() - 36, getWidth(), 36);

    g.setColour(juce::Colour::fromRGB(35, 45, 60));
    g.drawHorizontalLine(getHeight() - 36, 0.0f, static_cast<float>(getWidth()));
}

void PluginEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header (46px)
    auto headerArea = bounds.removeFromTop(46).reduced(6, 6);

    lblTitle_.setBounds(headerArea.removeFromLeft(175).reduced(0, 4));

    btnLoadVideo_.setBounds(headerArea.removeFromLeft(110).reduced(2, 2));
    btnCloseVideo_.setBounds(headerArea.removeFromLeft(65).reduced(2, 2));

    headerArea.removeFromLeft(8);
    lblOffset_.setBounds(headerArea.removeFromLeft(46).reduced(0, 3));
    sliderOffset_.setBounds(headerArea.removeFromLeft(125).reduced(0, 1));
    btnResetOffset_.setBounds(headerArea.removeFromLeft(36).reduced(2, 2));

    headerArea.removeFromLeft(8);
    lblSmpte_.setBounds(headerArea.removeFromLeft(50).reduced(0, 3));
    cmbSmpteRate_.setBounds(headerArea.removeFromLeft(105).reduced(0, 2));

    cmbSyncMode_.setBounds(headerArea.removeFromRight(135).reduced(2, 2));
    btnToggleOpenGL_.setBounds(headerArea.removeFromRight(85).reduced(0, 3));
    btnToggleHUD_.setBounds(headerArea.removeFromRight(55).reduced(0, 3));

    // Bottom Transport Bar (36px)
    auto transportArea = bounds.removeFromBottom(36).reduced(6, 4);
    btnRewind_.setBounds(transportArea.removeFromLeft(34).reduced(1, 1));
    btnPlayPause_.setBounds(transportArea.removeFromLeft(90).reduced(2, 1));
    lblHostStatus_.setBounds(transportArea.removeFromLeft(135).reduced(2, 1));
    lblTimelinePos_.setBounds(transportArea.removeFromRight(95).reduced(2, 1));
    sliderTimeline_.setBounds(transportArea.reduced(4, 0));

    // Main Video Viewport takes the entire middle area!
    videoViewport_.setBounds(bounds);
}

void PluginEditor::timerCallback()
{
    processor_.getTempoTracker().checkWatchdog(juce::Time::getMillisecondCounterHiRes() * 0.001);

    auto snapshot = processor_.getTempoTracker().getSnapshot();
    auto smpteRate = processor_.getSMPTEFrameRate();

    if (snapshot.isPlaying)
    {
        if (isManualPlaying_)
        {
            isManualPlaying_ = false;
        }
        btnPlayPause_.setButtonText("⏸ Playing");
        btnPlayPause_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(0, 180, 130));
        lblHostStatus_.setText("● " + snapshot.activeSource, juce::dontSendNotification);
        lblHostStatus_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(0, 240, 140));
    }
    else
    {
        if (isManualPlaying_)
        {
            double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            double currentPts = manualPlayStartPts_ + (now - manualPlayStartWallTime_);
            processor_.getDecoder().setPlaybackState(true, currentPts);
            btnPlayPause_.setButtonText("⏸ Pause");
            btnPlayPause_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(200, 120, 0));
            lblHostStatus_.setText("▶ Manual Play", juce::dontSendNotification);
            lblHostStatus_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 180, 50));
        }
        else
        {
            btnPlayPause_.setButtonText("▶ Play");
            btnPlayPause_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(0, 140, 100));
            lblHostStatus_.setText("○ " + snapshot.activeSource, juce::dontSendNotification);
            lblHostStatus_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(140, 160, 180));
        }
    }

    // Update timeline slider and time position label
    double decodedPts = processor_.getDecoder().getCurrentDecodedPts();
    auto metadata = processor_.getDecoder().getMetadata();
    double duration = (metadata.durationSeconds > 0.0) ? metadata.durationSeconds : 100.0;

    if (std::abs(sliderTimeline_.getMaximum() - duration) > 0.001)
    {
        sliderTimeline_.setRange(0.0, duration, 0.01);
    }

    if (!sliderTimeline_.isMouseButtonDown())
    {
        sliderTimeline_.setValue(decodedPts, juce::dontSendNotification);
    }

    int curM = static_cast<int>(decodedPts / 60.0);
    int curS = static_cast<int>(decodedPts) % 60;
    int durM = static_cast<int>(duration / 60.0);
    int durS = static_cast<int>(duration) % 60;
    lblTimelinePos_.setText(juce::String::formatted("%02d:%02d / %02d:%02d", curM, curS, durM, durS),
                            juce::dontSendNotification);

    videoViewport_.updateHUD(snapshot, smpteRate);
}

bool PluginEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (files.size() != 1)
        return false;

    juce::File f(files[0]);
    juce::String ext = f.getFileExtension().toLowerCase();

    return ext == ".mov" || ext == ".mp4" || ext == ".m4v" ||
           ext == ".mkv" || ext == ".avi" || ext == ".webm" || ext == ".prores";
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
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select Video Score File (ProRes, H.264, HEVC)",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.mov;*.mp4;*.m4v;*.mkv;*.avi;*.webm");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser) {
        auto result = chooser.getResult();
        if (result.existsAsFile())
        {
            processor_.loadVideoFile(result);
        }
    });
}

void PluginEditor::closeCurrentVideo()
{
    processor_.getDecoder().closeFile();
}

void PluginEditor::videoLoaded(const VideoMetadata& /*metadata*/)
{
    repaint();
}

void PluginEditor::videoLoadFailed(const juce::String& errorMessage)
{
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Video Load Error")
            .withMessage(errorMessage)
            .withButton("OK"),
        nullptr);
}

} // namespace MScoreVideoSync
