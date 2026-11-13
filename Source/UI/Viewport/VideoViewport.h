#pragma once

#include "../../Core/CommonTypes.h"
#include "../../Video/VideoDecodeEngine.h"
#include "../../Cues/CueMarkerModel.h"
#include "OverlayRenderer.h"
#include "../Theme/Vep8Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

namespace MScoreVideoSync
{

class VideoViewport : public juce::Component
{
public:
    VideoViewport(VideoDecodeEngine& decoder, CueMarkerModel& markerModel);
    ~VideoViewport() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void updateSnapshot(const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate);

    // Aspect Ratio & Zoom
    void setAspectRatioMode(AspectRatioMode mode);
    AspectRatioMode getAspectRatioMode() const noexcept { return aspectMode_; }

    void setZoomPreset(VideoZoomPreset zoom);
    VideoZoomPreset getZoomPreset() const noexcept { return zoomPreset_; }

    // Overlay access
    OverlayRenderer& getOverlay() noexcept { return overlay_; }

    // Toggle OpenGL
    void setOpenGLRendering(bool useOpenGL);
    bool isOpenGLRendering() const noexcept { return useOpenGL_; }

private:
    void drawEmptyState(juce::Graphics& g);
    juce::Rectangle<int> calculateVideoBounds() const;

    VideoDecodeEngine& decoder_;
    CueMarkerModel& markerModel_;
    OverlayRenderer overlay_;

    TransportSnapshot currentSnapshot_;
    SMPTEFrameRate currentSmpteRate_{ SMPTEFrameRate::FPS_24 };

    AspectRatioMode aspectMode_{ AspectRatioMode::Native };
    VideoZoomPreset zoomPreset_{ VideoZoomPreset::FitToScreen };

    bool useOpenGL_{ false };
    juce::OpenGLContext openGLContext_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoViewport)
};

} // namespace MScoreVideoSync
