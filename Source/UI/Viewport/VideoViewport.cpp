#include "VideoViewport.h"

namespace MScoreVideoSync
{

VideoViewport::VideoViewport(VideoDecodeEngine& decoder, CueMarkerModel& markerModel)
    : decoder_(decoder), markerModel_(markerModel)
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

void VideoViewport::setAspectRatioMode(AspectRatioMode mode)
{
    aspectMode_ = mode;
    repaint();
}

void VideoViewport::setZoomPreset(VideoZoomPreset zoom)
{
    zoomPreset_ = zoom;
    repaint();
}

void VideoViewport::updateSnapshot(const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate)
{
    currentSnapshot_ = snapshot;
    currentSmpteRate_ = smpteRate;
    repaint();
}

void VideoViewport::resized()
{
}

juce::Rectangle<int> VideoViewport::calculateVideoBounds() const
{
    auto bounds = getLocalBounds();
    auto metadata = decoder_.getMetadata();

    double aspect = metadata.getAspectRatio();
    if (aspectMode_ == AspectRatioMode::Ratio_16_9) aspect = 16.0 / 9.0;
    else if (aspectMode_ == AspectRatioMode::Ratio_2_39_1) aspect = 2.39;
    else if (aspectMode_ == AspectRatioMode::Ratio_1_85_1) aspect = 1.85;
    else if (aspectMode_ == AspectRatioMode::Ratio_4_3) aspect = 4.0 / 3.0;

    if (aspectMode_ == AspectRatioMode::FillStretch)
    {
        return bounds;
    }

    int availW = bounds.getWidth();
    int availH = bounds.getHeight();

    int targetW = availW;
    int targetH = static_cast<int>(targetW / aspect);

    if (targetH > availH)
    {
        targetH = availH;
        targetW = static_cast<int>(targetH * aspect);
    }

    // Apply zoom preset scale
    float scale = 1.0f;
    if (zoomPreset_ == VideoZoomPreset::Zoom_50) scale = 0.5f;
    else if (zoomPreset_ == VideoZoomPreset::Zoom_100) scale = 1.0f;
    else if (zoomPreset_ == VideoZoomPreset::Zoom_150) scale = 1.5f;
    else if (zoomPreset_ == VideoZoomPreset::Zoom_200) scale = 2.0f;

    if (zoomPreset_ != VideoZoomPreset::FitToScreen)
    {
        targetW = static_cast<int>(static_cast<float>(metadata.width) * scale);
        targetH = static_cast<int>(static_cast<float>(metadata.height) * scale);
    }

    int targetX = (availW - targetW) / 2;
    int targetY = (availH - targetH) / 2;

    return juce::Rectangle<int>(targetX, targetY, targetW, targetH);
}

void VideoViewport::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Backdrop
    g.setColour(Vep8Theme::bgMain);
    g.fillRect(bounds);

    if (!decoder_.isFileLoaded())
    {
        drawEmptyState(g);
        return;
    }

    auto videoRect = calculateVideoBounds();
    const DecodedVideoFrame* frame = decoder_.getLatestFrame();

    if (frame && frame->isValid && frame->image.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(frame->image,
                    videoRect.toFloat(),
                    juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }
    else
    {
        g.setColour(Vep8Theme::bgPanel);
        g.fillRect(videoRect);

        g.setColour(Vep8Theme::textSecondary);
        g.setFont(Vep8Theme::getUiFont(14.0f));
        g.drawText("Buffering Video Frame...", videoRect, juce::Justification::centred);
    }

    // Render Overlay (OSD, Streamers, Punches)
    overlay_.render(g, videoRect, currentSnapshot_, currentSmpteRate_, decoder_.getMetadata(), markerModel_);
}

void VideoViewport::drawEmptyState(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient grad(Vep8Theme::bgPanel.brighter(0.08f), bounds.getCentreX(), bounds.getCentreY(),
                              Vep8Theme::bgMain, 0.0f, 0.0f, true);
    g.setGradientFill(grad);
    g.fillRect(bounds);

    auto centerBox = bounds.reduced(bounds.getWidth() * 0.15f, bounds.getHeight() * 0.2f);
    g.setColour(Vep8Theme::borderLight.withAlpha(0.6f));
    float dashPattern[2] = { 8.0f, 6.0f };
    g.drawDashedLine(juce::Line<float>(centerBox.getX(), centerBox.getY(), centerBox.getRight(), centerBox.getY()), dashPattern, 2, 1.5f);
    g.drawDashedLine(juce::Line<float>(centerBox.getRight(), centerBox.getY(), centerBox.getRight(), centerBox.getBottom()), dashPattern, 2, 1.5f);
    g.drawDashedLine(juce::Line<float>(centerBox.getRight(), centerBox.getBottom(), centerBox.getX(), centerBox.getBottom()), dashPattern, 2, 1.5f);
    g.drawDashedLine(juce::Line<float>(centerBox.getX(), centerBox.getBottom(), centerBox.getX(), centerBox.getY()), dashPattern, 2, 1.5f);

    // Film Slate Icon
    auto iconArea = centerBox.withHeight(50).translated(0, 20);
    g.setColour(Vep8Theme::cyan);
    g.drawRoundedRectangle(iconArea.withSizeKeepingCentre(44, 32), 4.0f, 2.0f);
    g.fillEllipse(iconArea.getCentreX() - 6, iconArea.getCentreY() - 6, 12, 12);

    g.setColour(Vep8Theme::textPrimary);
    g.setFont(Vep8Theme::getUiFont(17.0f, true));
    g.drawText("Drop Video File Here", centerBox.translated(0, 56), juce::Justification::centredTop);

    g.setColour(Vep8Theme::textSecondary);
    g.setFont(Vep8Theme::getUiFont(13.0f, false));
    g.drawText("or click 'Load Video' in top bar", centerBox.translated(0, 82), juce::Justification::centredTop);

    g.setColour(Vep8Theme::textDim);
    g.setFont(Vep8Theme::getUiFont(11.0f, false));
    g.drawText("Vienna Ensemble Pro 8 Architecture • Apple ProRes, DNxHR, H.264, HEVC / H.265 (Hardware Accelerated)",
               centerBox.withHeight(30).withBottom(centerBox.getBottom() - 10),
               juce::Justification::centred);
}

} // namespace MScoreVideoSync
