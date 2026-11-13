#include "FloatingVideoWindow.h"

namespace MScoreVideoSync
{

FloatingVideoWindow::FloatingVideoWindow(const juce::String& name,
                                         VideoDecodeEngine& decoder,
                                         CueMarkerModel& markerModel)
    : juce::DocumentWindow(name,
                           Vep8Theme::bgMain,
                           juce::DocumentWindow::allButtons),
      viewport_(decoder, markerModel)
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&viewport_, true);
    setResizable(true, true);
    setResizeLimits(320, 180, 3840, 2160);

    // Default 16:9 720p size
    centreWithSize(960, 540);

    setAlwaysOnTop(true);
    isAlwaysOnTop_ = true;

    startTimerHz(60);
}

FloatingVideoWindow::~FloatingVideoWindow()
{
    stopTimer();
    clearContentComponent();
}

void FloatingVideoWindow::closeButtonPressed()
{
    setVisible(false);
}

void FloatingVideoWindow::timerCallback()
{
    viewport_.updateSnapshot(currentSnapshot_, currentSmpteRate_);
}

void FloatingVideoWindow::updateSnapshot(const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate)
{
    currentSnapshot_ = snapshot;
    currentSmpteRate_ = smpteRate;
}

void FloatingVideoWindow::setAlwaysOnTopActive(bool alwaysOnTop)
{
    isAlwaysOnTop_ = alwaysOnTop;
    setAlwaysOnTop(alwaysOnTop);
}

void FloatingVideoWindow::setBorderlessActive(bool borderless)
{
    isBorderless_ = borderless;
    setUsingNativeTitleBar(!borderless);
    setTitleBarHeight(borderless ? 0 : 26);
}

void FloatingVideoWindow::setClickThroughActive(bool clickThrough)
{
    isClickThrough_ = clickThrough;
    setInterceptsMouseClicks(!clickThrough, !clickThrough);
}

void FloatingVideoWindow::setOpacityLevel(float opacity)
{
    opacity_ = juce::jlimit(0.2f, 1.0f, opacity);
    setAlpha(opacity_);
}

juce::String FloatingVideoWindow::getWindowGeometryString()
{
    return getWindowStateAsString();
}

void FloatingVideoWindow::restoreWindowGeometry(const juce::String& geom)
{
    if (geom.isNotEmpty())
    {
        restoreWindowStateFromString(geom);
    }
}

} // namespace MScoreVideoSync
