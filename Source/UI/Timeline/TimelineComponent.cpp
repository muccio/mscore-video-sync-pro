#include "TimelineComponent.h"

namespace MScoreVideoSync
{

TimelineComponent::TimelineComponent(TransportSyncEngine& syncEngine,
                                     VideoDecodeEngine& decoder,
                                     CueMarkerModel& markerModel)
    : syncEngine_(syncEngine), decoder_(decoder), markerModel_(markerModel)
{
    markerModel_.addListener(this);
    decoder_.getFilmstripGenerator().addListener(this);
    decoder_.getWaveformExtractor().addListener(this);
}

TimelineComponent::~TimelineComponent()
{
    markerModel_.removeListener(this);
    decoder_.getFilmstripGenerator().removeListener(this);
    decoder_.getWaveformExtractor().removeListener(this);
}

void TimelineComponent::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void TimelineComponent::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void TimelineComponent::markersChanged()
{
    repaint();
}

void TimelineComponent::filmstripUpdated()
{
    juce::Component::SafePointer<TimelineComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]()
    {
        if (safeThis != nullptr)
            safeThis->repaint();
    });
}

void TimelineComponent::waveformReady()
{
    juce::Component::SafePointer<TimelineComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]()
    {
        if (safeThis != nullptr)
            safeThis->repaint();
    });
}

void TimelineComponent::updateSnapshot(const TransportSnapshot& snapshot, SMPTEFrameRate rate)
{
    currentSnapshot_ = snapshot;
    currentSmpteRate_ = rate;

    auto meta = decoder_.getMetadata();
    double totalDur = meta.durationSeconds > 0.0 ? meta.durationSeconds : 120.0;
    viewDurationSec_ = totalDur / zoomFactor_;

    // Keep playhead in view if playing
    if (snapshot.isPlaying)
    {
        double currentPts = snapshot.getTargetVideoPts();
        if (currentPts < viewStartSec_ || currentPts > (viewStartSec_ + viewDurationSec_))
        {
            viewStartSec_ = std::max(0.0, currentPts - (viewDurationSec_ * 0.15));
        }
    }

    repaint();
}

void TimelineComponent::setZoomFactor(double factor)
{
    zoomFactor_ = juce::jlimit(0.25, 20.0, factor);
    repaint();
}

void TimelineComponent::resized()
{
}

double TimelineComponent::xToTime(float x) const noexcept
{
    float w = static_cast<float>(getWidth());
    if (w <= 0.0f) return 0.0;
    double ratio = juce::jlimit(0.0, 1.0, static_cast<double>(x / w));
    return viewStartSec_ + (ratio * viewDurationSec_);
}

float TimelineComponent::timeToX(double time) const noexcept
{
    float w = static_cast<float>(getWidth());
    if (viewDurationSec_ <= 0.0) return 0.0f;
    double offset = time - viewStartSec_;
    return static_cast<float>((offset / viewDurationSec_) * w);
}

void TimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    float x = static_cast<float>(e.x);
    float y = static_cast<float>(e.y);

    // Check if clicked a marker in Marker lane (height 28 to 48)
    if (y >= 28.0f && y <= 50.0f)
    {
        auto markers = markerModel_.getMarkersSorted();
        for (const auto& m : markers)
        {
            float mx = timeToX(m.timeSeconds);
            if (std::abs(x - mx) <= 8.0f)
            {
                draggingMarkerId_ = m.id;
                listeners_.call([&](Listener& l) { l.markerSelected(m.id); });
                return;
            }
        }
    }

    isDraggingPlayhead_ = true;
    double targetTime = xToTime(x);
    syncEngine_.freeScrubToTime(targetTime);
    listeners_.call([&](Listener& l) { l.timelineScrubbed(targetTime); });
    repaint();
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& e)
{
    float x = static_cast<float>(e.x);
    double targetTime = xToTime(x);

    if (draggingMarkerId_ > 0)
    {
        CueMarker m;
        if (markerModel_.getMarkerById(draggingMarkerId_, m))
        {
            m.timeSeconds = std::max(0.0, targetTime);
            double bpm = currentSnapshot_.bpm > 0.0 ? currentSnapshot_.bpm : 120.0;
            m.ppqPosition = (m.timeSeconds * bpm) / 60.0;
            TransportSyncEngine::calculateBarBeat(m.ppqPosition, currentSnapshot_.timeSigNumerator, currentSnapshot_.timeSigDenominator, m.bar, m.beat);
            markerModel_.updateMarker(m);
        }
    }
    else if (isDraggingPlayhead_)
    {
        syncEngine_.freeScrubToTime(targetTime);
        listeners_.call([&](Listener& l) { l.timelineScrubbed(targetTime); });
    }
    repaint();
}

void TimelineComponent::mouseUp(const juce::MouseEvent& /*e*/)
{
    isDraggingPlayhead_ = false;
    draggingMarkerId_ = -1;
}

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isAltDown())
    {
        // Horizontal Zoom
        double newZoom = zoomFactor_ * (1.0 + (wheel.deltaY * 0.5));
        setZoomFactor(newZoom);
    }
    else
    {
        // Horizontal Pan or 1-frame step
        if (std::abs(wheel.deltaY) > 0.02f)
        {
            int frames = (wheel.deltaY > 0) ? 1 : -1;
            syncEngine_.stepFrames(frames, currentSmpteRate_);
        }
    }
}

void TimelineComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    double targetTime = xToTime(static_cast<float>(e.x));
    double bpm = currentSnapshot_.bpm > 0.0 ? currentSnapshot_.bpm : 120.0;
    double ppq = (targetTime * bpm) / 60.0;
    int bar = 1;
    double beat = 1.0;
    TransportSyncEngine::calculateBarBeat(ppq, currentSnapshot_.timeSigNumerator, currentSnapshot_.timeSigDenominator, bar, beat);

    markerModel_.addQuickMarker(targetTime, ppq, bar, beat);
}

void TimelineComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(Vep8Theme::bgPanel);
    g.fillRect(bounds);

    float rulerH = 26.0f;
    float markerH = 22.0f;
    float filmstripH = 36.0f;

    auto rulerArea = bounds.removeFromTop(rulerH);
    auto markerArea = bounds.removeFromTop(markerH);
    auto filmstripArea = bounds.removeFromTop(filmstripH);
    auto waveformArea = bounds;

    drawDualRuler(g, rulerArea);
    drawMarkerLane(g, markerArea);
    drawFilmstripLane(g, filmstripArea);
    drawWaveformLane(g, waveformArea);
    drawPlayhead(g, getLocalBounds().toFloat());
}

void TimelineComponent::drawDualRuler(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    g.setColour(Vep8Theme::bgInput);
    g.fillRect(area);
    g.setColour(Vep8Theme::border);
    g.drawHorizontalLine(static_cast<int>(area.getBottom()), area.getX(), area.getRight());

    // Ticks & numbers
    double secStep = 1.0;
    if (viewDurationSec_ > 120.0) secStep = 10.0;
    else if (viewDurationSec_ > 30.0) secStep = 5.0;
    else if (viewDurationSec_ > 10.0) secStep = 2.0;
    else secStep = 1.0;

    double firstTick = std::floor(viewStartSec_ / secStep) * secStep;
    for (double t = firstTick; t <= viewStartSec_ + viewDurationSec_; t += secStep)
    {
        float x = timeToX(t);
        if (x < area.getX() || x > area.getRight()) continue;

        g.setColour(Vep8Theme::borderLight);
        g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());

        SMPTETimecode tc = SMPTETimecode::fromSeconds(t, currentSmpteRate_);
        g.setColour(Vep8Theme::textSecondary);
        g.setFont(Vep8Theme::getMonospaceFont(9.5f, false));
        g.drawText(tc.toString(false), juce::Rectangle<float>(x + 3.0f, area.getY() + 1.0f, 70.0f, 12.0f), juce::Justification::topLeft);

        // Musical bar label below
        double bpm = currentSnapshot_.bpm > 0.0 ? currentSnapshot_.bpm : 120.0;
        double ppq = (t * bpm) / 60.0;
        int bar = 1;
        double beat = 1.0;
        TransportSyncEngine::calculateBarBeat(ppq, currentSnapshot_.timeSigNumerator, currentSnapshot_.timeSigDenominator, bar, beat);
        g.setColour(Vep8Theme::cyan.withAlpha(0.7f));
        g.drawText(juce::String::formatted("m.%d", bar), juce::Rectangle<float>(x + 3.0f, area.getY() + 13.0f, 40.0f, 11.0f), juce::Justification::topLeft);
    }
}

void TimelineComponent::drawMarkerLane(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    g.setColour(Vep8Theme::bgPanel.brighter(0.02f));
    g.fillRect(area);
    g.setColour(Vep8Theme::border);
    g.drawHorizontalLine(static_cast<int>(area.getBottom()), area.getX(), area.getRight());

    auto markers = markerModel_.getMarkersSorted();
    for (const auto& m : markers)
    {
        float hitX = timeToX(m.timeSeconds);

        // Streamer duration shaded zone before marker
        if (m.streamerEnabled)
        {
            double hitPts = m.getEffectivePts(currentSnapshot_.bpm, 4);
            double startPts = m.getStreamerStartPts(hitPts, currentSnapshot_.bpm);
            float startX = timeToX(startPts);
            if (hitX > startX)
            {
                g.setColour(m.color.withAlpha(0.2f));
                g.fillRect(startX, area.getY(), hitX - startX, area.getHeight());
            }
        }

        if (hitX >= area.getX() - 10.0f && hitX <= area.getRight() + 10.0f)
        {
            // Marker flag
            g.setColour(m.color);
            juce::Path flag;
            flag.addTriangle(hitX, area.getBottom(),
                             hitX - 6.0f, area.getY() + 4.0f,
                             hitX + 6.0f, area.getY() + 4.0f);
            flag.addRectangle(hitX - 6.0f, area.getY(), 12.0f, 5.0f);
            g.fillPath(flag);

            // Cue title
            g.setColour(Vep8Theme::textPrimary);
            g.setFont(Vep8Theme::getUiFont(9.5f, true));
            g.drawText(m.name, juce::Rectangle<float>(hitX + 8.0f, area.getY() + 2.0f, 80.0f, 16.0f), juce::Justification::centredLeft);
        }
    }
}

void TimelineComponent::drawFilmstripLane(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    g.setColour(Vep8Theme::bgMain);
    g.fillRect(area);
    g.setColour(Vep8Theme::border);
    g.drawHorizontalLine(static_cast<int>(area.getBottom()), area.getX(), area.getRight());

    auto& fg = decoder_.getFilmstripGenerator();
    const double thumbInterval = std::max(0.5, fg.getIntervalSeconds());
    float thumbW = 56.0f;
    float thumbH = area.getHeight();

    int startThumbIdx = static_cast<int>(std::floor(viewStartSec_ / thumbInterval));
    int endThumbIdx = static_cast<int>(std::ceil((viewStartSec_ + viewDurationSec_) / thumbInterval));

    for (int idx = startThumbIdx; idx <= endThumbIdx; ++idx)
    {
        double t = idx * thumbInterval;
        float x = timeToX(t);
        auto thumb = fg.getThumbnailForTime(t, thumbInterval);

        if (thumb.isValid())
        {
            g.drawImage(thumb, juce::Rectangle<float>(x, area.getY(), thumbW, thumbH));
        }
        else
        {
            g.setColour(Vep8Theme::bgInput);
            g.fillRect(x, area.getY(), thumbW - 1.0f, thumbH);
        }
    }
}

void TimelineComponent::drawWaveformLane(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    g.setColour(Vep8Theme::bgPanel.darker(0.1f));
    g.fillRect(area);

    auto& we = decoder_.getWaveformExtractor();
    auto peaks = we.getPeaksSnapshot();
    if (peaks.empty())
    {
        g.setColour(Vep8Theme::textDim);
        g.setFont(Vep8Theme::getUiFont(11.0f, false));
        g.drawText("No Audio Track in Video", area, juce::Justification::centred);
        return;
    }

    double res = we.getTimeResolution();
    float midY = area.getCentreY();
    float halfH = area.getHeight() * 0.45f;

    g.setColour(Vep8Theme::cyan.withAlpha(0.65f));

    for (size_t i = 0; i < peaks.size(); ++i)
    {
        double t = static_cast<double>(i) * res;
        if (t < viewStartSec_ || t > (viewStartSec_ + viewDurationSec_)) continue;

        float x = timeToX(t);
        float topY = midY - (peaks[i].maxVal * halfH);
        float bottomY = midY - (peaks[i].minVal * halfH);

        g.drawVerticalLine(static_cast<int>(x), topY, std::max(topY + 1.0f, bottomY));
    }
}

void TimelineComponent::drawPlayhead(juce::Graphics& g, const juce::Rectangle<float>& area)
{
    double targetPts = currentSnapshot_.getTargetVideoPts();
    float px = timeToX(targetPts);

    if (px >= area.getX() && px <= area.getRight())
    {
        // Playhead vertical needle
        g.setColour(Vep8Theme::cyan);
        g.fillRect(px - 1.0f, area.getY(), 2.0f, area.getHeight());

        // Playhead diamond handle at top
        juce::Path diamond;
        diamond.addTriangle(px, area.getY() + 10.0f,
                            px - 5.0f, area.getY(),
                            px + 5.0f, area.getY());
        g.fillPath(diamond);
    }
}

} // namespace MScoreVideoSync
