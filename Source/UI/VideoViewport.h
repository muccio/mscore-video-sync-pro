#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include "../Core/CommonTypes.h"
#include "../Video/FFmpegDecoder.h"

namespace MScoreVideoSync
{

class VideoViewport : public juce::Component
{
public:
    VideoViewport(FFmpegDecoder& decoder);
    ~VideoViewport() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Update overlay display information
    void updateHUD(const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate);

    // Toggle HUD visibility
    void setHUDVisible(bool visible);
    bool isHUDVisible() const noexcept { return showHud_; }

    // Toggle OpenGL hardware renderer
    void setOpenGLRendering(bool useOpenGL);
    bool isOpenGLRendering() const noexcept { return useOpenGL_; }

private:
    void drawHUD(juce::Graphics& g, const juce::Rectangle<int>& videoArea);
    void drawEmptyState(juce::Graphics& g);

    FFmpegDecoder& decoder_;

    TransportSnapshot currentSnapshot_;
    SMPTEFrameRate currentSmpteRate_{ SMPTEFrameRate::FPS_24 };

    bool showHud_{ true };
    bool useOpenGL_{ false };
    float hudOpacity_{ 1.0f };

    juce::OpenGLContext openGLContext_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoViewport)
};

} // namespace MScoreVideoSync
