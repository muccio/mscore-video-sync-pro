#include "VideoViewport.h"
#include "../Core/TempoMapTracker.h"

namespace MScoreVideoSync
{

VideoViewport::VideoViewport(FFmpegDecoder& decoder)
    : decoder_(decoder)
{
    setOpaque(true);
}

VideoViewport::~VideoViewport()
{
    openGLContext_.detach();
}

void VideoViewport::setOpenGLRendering(bool useOpenGL)
{
    useOpenGL_ = useOpenGL;
    if (useOpenGL_)
    {
        openGLContext_.attachTo(*this);
    }
    else
    {
        openGLContext_.detach();
    }
}

void VideoViewport::setHUDVisible(bool visible)
{
    showHud_ = visible;
    repaint();
}

void VideoViewport::updateHUD(const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate)
{
    currentSnapshot_ = snapshot;
    currentSmpteRate_ = smpteRate;
    repaint();
}

void VideoViewport::mouseMove(const juce::MouseEvent& /*e*/)
{
    hudOpacity_ = 1.0f;
    repaint();
}

void VideoViewport::mouseExit(const juce::MouseEvent& /*e*/)
{
    // Keep HUD visible if enabled
    repaint();
}

void VideoViewport::resized()
{
}

void VideoViewport::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Matte background
    g.setColour(juce::Colour::fromRGB(14, 16, 20));
    g.fillRect(bounds);

    if (!decoder_.isFileLoaded())
    {
        drawEmptyState(g);
        return;
    }

    const DecodedVideoFrame* frame = decoder_.getLatestFrame();
    auto metadata = decoder_.getMetadata();

    double aspect = metadata.getAspectRatio();
    if (aspect <= 0.0) aspect = 16.0 / 9.0;

    int availW = bounds.getWidth();
    int availH = bounds.getHeight();

    int targetW = availW;
    int targetH = static_cast<int>(targetW / aspect);

    if (targetH > availH)
    {
        targetH = availH;
        targetW = static_cast<int>(targetH * aspect);
    }

    int targetX = (availW - targetW) / 2;
    int targetY = (availH - targetH) / 2;

    juce::Rectangle<int> videoRect(targetX, targetY, targetW, targetH);

    if (frame && frame->isValid && frame->image.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(frame->image,
                    videoRect.toFloat(),
                    juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }
    else
    {
        g.setColour(juce::Colour::fromRGB(20, 24, 30));
        g.fillRect(videoRect);

        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText("Buffering Video...", videoRect, juce::Justification::centred);
    }

    if (showHud_)
    {
        drawHUD(g, videoRect);
    }
}

void VideoViewport::drawEmptyState(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Radial gradient glow in background
    juce::ColourGradient grad(juce::Colour::fromRGB(22, 32, 48), bounds.getCentreX(), bounds.getCentreY(),
                              juce::Colour::fromRGB(10, 12, 16), 0.0f, 0.0f, true);
    g.setGradientFill(grad);
    g.fillRect(bounds);

    // Decorative dashed outline
    auto centerBox = bounds.reduced(bounds.getWidth() * 0.15f, bounds.getHeight() * 0.2f);
    g.setColour(juce::Colour::fromRGB(45, 60, 80).withAlpha(0.6f));
    float dashPattern[2] = { 8.0f, 6.0f };
    g.drawDashedLine(juce::Line<float>(centerBox.getX(), centerBox.getY(), centerBox.getRight(), centerBox.getY()), dashPattern, 2, 2.0f);
    g.drawDashedLine(juce::Line<float>(centerBox.getRight(), centerBox.getY(), centerBox.getRight(), centerBox.getBottom()), dashPattern, 2, 2.0f);
    g.drawDashedLine(juce::Line<float>(centerBox.getRight(), centerBox.getBottom(), centerBox.getX(), centerBox.getBottom()), dashPattern, 2, 2.0f);
    g.drawDashedLine(juce::Line<float>(centerBox.getX(), centerBox.getBottom(), centerBox.getX(), centerBox.getY()), dashPattern, 2, 2.0f);

    // Film Icon / Camera Icon
    auto iconArea = centerBox.withHeight(60).translated(0, 20);
    g.setColour(juce::Colour::fromRGB(0, 210, 255));
    g.drawRoundedRectangle(iconArea.withSizeKeepingCentre(48, 36), 4.0f, 2.5f);
    g.fillEllipse(iconArea.getCentreX() - 8, iconArea.getCentreY() - 8, 16, 16);

    // Text instructions
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("Drop Video File Here", centerBox.translated(0, 60), juce::Justification::centredTop);

    g.setColour(juce::Colour::fromRGB(160, 180, 205));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("or click 'Load Video' below", centerBox.translated(0, 88), juce::Justification::centredTop);

    g.setColour(juce::Colour::fromRGB(100, 120, 140));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Supports Apple ProRes, H.264, HEVC / H.265 (Hardware Accelerated via VideoToolbox)",
               centerBox.withHeight(30).withBottom(centerBox.getBottom() - 10),
               juce::Justification::centred);
}

void VideoViewport::drawHUD(juce::Graphics& g, const juce::Rectangle<int>& videoArea)
{
    // Top Info Bar
    auto topBar = videoArea.withHeight(32).toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillRect(topBar);

    auto metadata = decoder_.getMetadata();

    // Hardware acceleration badge (VideoToolbox vs Software)
    bool isHw = decoder_.isHardwareAccelerated();
    juce::Colour badgeColor = isHw ? juce::Colour::fromRGB(0, 230, 130) : juce::Colour::fromRGB(255, 175, 40);
    juce::String badgeText = isHw ? "VideoToolbox HW [NV12]" : "Software [CPU]";

    auto hwBadgeRect = topBar.removeFromRight(180).reduced(4, 4);
    g.setColour(badgeColor.withAlpha(0.2f));
    g.fillRoundedRectangle(hwBadgeRect, 4.0f);
    g.setColour(badgeColor);
    g.drawRoundedRectangle(hwBadgeRect, 4.0f, 1.0f);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(badgeText, hwBadgeRect, juce::Justification::centred);

    // Video metadata info
    juce::File vFile(metadata.filePath);
    juce::String fileName = vFile.getFileName();
    juce::String infoText;
    if (fileName.isNotEmpty())
    {
        infoText = fileName + "  •  " + juce::String::formatted("%dx%d | %.2f fps | %s",
                                                               metadata.width,
                                                               metadata.height,
                                                               metadata.frameRate,
                                                               metadata.codecName.toRawUTF8());
    }
    else
    {
        infoText = juce::String::formatted("%dx%d | %.2f fps | %s",
                                           metadata.width,
                                           metadata.height,
                                           metadata.frameRate,
                                           metadata.codecName.toRawUTF8());
    }
    g.setColour(juce::Colour::fromRGB(200, 220, 240));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(infoText, topBar.reduced(10, 0), juce::Justification::centredLeft);

    // Bottom HUD Glass Box (SMPTE Timecode + Frame / Seconds)
    float hudW = static_cast<float>(std::min(420, videoArea.getWidth() - 20));
    float hudH = 58.0f;
    juce::Rectangle<float> hudBox(static_cast<float>(videoArea.getCentreX()) - hudW * 0.5f,
                                 static_cast<float>(videoArea.getBottom()) - hudH - 12.0f,
                                 hudW, hudH);

    // Glass backdrop
    g.setColour(juce::Colour::fromRGB(10, 14, 20).withAlpha(0.85f));
    g.fillRoundedRectangle(hudBox, 8.0f);
    g.setColour(juce::Colour::fromRGB(0, 200, 255).withAlpha(0.35f));
    g.drawRoundedRectangle(hudBox, 8.0f, 1.5f);

    // SMPTE readout
    double targetPts = currentSnapshot_.getTargetVideoPts();
    SMPTETimecode tc = SMPTETimecode::fromSeconds(targetPts, currentSmpteRate_);
    juce::String smpteStr = tc.toString(true);

    auto smpteArea = hudBox.removeFromLeft(hudBox.getWidth() * 0.62f).reduced(8, 4);

    g.setColour(juce::Colour::fromRGB(120, 150, 180));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("SMPTE TIMECODE (" + juce::String(getFrameRateLabel(currentSmpteRate_)) + ")",
               smpteArea.withHeight(14), juce::Justification::topLeft);

    g.setColour(juce::Colour::fromRGB(0, 240, 255));
    g.setFont(juce::FontOptions("Monaco", 22.0f, juce::Font::bold));
    g.drawText(smpteStr, smpteArea.withTrimmedTop(14), juce::Justification::centredLeft);

    // Divider
    g.setColour(juce::Colour::fromRGB(60, 80, 100).withAlpha(0.6f));
    g.fillRect(hudBox.getX(), hudBox.getY() + 8, 1.0f, hudBox.getHeight() - 16);

    // Frame Counter & Time Info
    auto frameArea = hudBox.reduced(8, 4);

    g.setColour(juce::Colour::fromRGB(120, 150, 180));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("FRAME COUNTER", frameArea.withHeight(14), juce::Justification::topLeft);

    double fps = getFrameRateValue(currentSmpteRate_);
    int64_t frameNum = static_cast<int64_t>(targetPts * fps);
    juce::String frameStr = juce::String::formatted("#%lld (%.3fs)", frameNum, targetPts);

    g.setColour(juce::Colour::fromRGB(255, 205, 80));
    g.setFont(juce::FontOptions("Monaco", 15.0f, juce::Font::bold));
    g.drawText(frameStr, frameArea.withTrimmedTop(14), juce::Justification::centredLeft);
}

} // namespace MScoreVideoSync
