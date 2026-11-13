#pragma once

#include "../../Core/CommonTypes.h"
#include "../../Video/VideoDecodeEngine.h"
#include "../../Cues/CueMarkerModel.h"
#include "../Viewport/VideoViewport.h"
#include "../Theme/Vep8Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace MScoreVideoSync
{

class FloatingVideoWindow : public juce::DocumentWindow,
                            public juce::Timer
{
public:
    FloatingVideoWindow(const juce::String& name,
                        VideoDecodeEngine& decoder,
                        CueMarkerModel& markerModel);
    ~FloatingVideoWindow() override;

    void closeButtonPressed() override;

    // Viewport updates (60Hz timer)
    void timerCallback() override;
    void updateSnapshot(const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate);

    // Window features
    void setAlwaysOnTopActive(bool alwaysOnTop);
    bool isAlwaysOnTopActive() const noexcept { return isAlwaysOnTop_; }

    void setBorderlessActive(bool borderless);
    bool isBorderlessActive() const noexcept { return isBorderless_; }

    void setClickThroughActive(bool clickThrough);
    bool isClickThroughActive() const noexcept { return isClickThrough_; }

    void setOpacityLevel(float opacity);
    float getOpacityLevel() const noexcept { return opacity_; }

    // Geometrical state serialization
    juce::String getWindowGeometryString();
    void restoreWindowGeometry(const juce::String& geom);

    VideoViewport& getViewport() noexcept { return viewport_; }

private:
    VideoViewport viewport_;
    TransportSnapshot currentSnapshot_;
    SMPTEFrameRate currentSmpteRate_{ SMPTEFrameRate::FPS_24 };

    bool isAlwaysOnTop_{ true };
    bool isBorderless_{ false };
    bool isClickThrough_{ false };
    float opacity_{ 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingVideoWindow)
};

} // namespace MScoreVideoSync
