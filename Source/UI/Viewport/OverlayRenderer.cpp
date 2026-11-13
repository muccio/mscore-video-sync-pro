#include "OverlayRenderer.h"
#include "../../Sync/TransportSyncEngine.h"

namespace MScoreVideoSync
{

OverlayRenderer::OverlayRenderer()
{
}

void OverlayRenderer::render(juce::Graphics& g,
                             const juce::Rectangle<int>& videoArea,
                             const TransportSnapshot& snapshot,
                             SMPTEFrameRate smpteRate,
                             const VideoMetadata& metadata,
                             const CueMarkerModel& markerModel)
{
    if (videoArea.isEmpty()) return;

    double currentPts = snapshot.getTargetVideoPts();
    double fps = getFrameRateValue(smpteRate);

    // 1. Draw Visual Cueing Engine (Streamers under OSD)
    if (showStreamers_)
    {
        drawStreamers(g, videoArea, currentPts, snapshot.bpm, markerModel);
    }

    // 2. Draw Punches & Flutters
    if (showPunches_)
    {
        drawPunchesAndFlutters(g, videoArea, currentPts, fps, snapshot.bpm, markerModel);
    }

    // 3. Draw Multi-zone OSD HUD
    if (showOsd_)
    {
        drawTopMetadataBar(g, videoArea, metadata, snapshot);
        drawBottomOsdHud(g, videoArea, snapshot, smpteRate, markerModel);
    }
}

void OverlayRenderer::drawTopMetadataBar(juce::Graphics& g, const juce::Rectangle<int>& videoArea, const VideoMetadata& metadata, const TransportSnapshot& snapshot)
{
    auto topBar = videoArea.withHeight(30).toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.6f * osdOpacity_));
    g.fillRect(topBar);

    // Left info: Video filename & resolution
    juce::File vf(metadata.filePath);
    juce::String fileTitle = vf.getFileName();
    if (fileTitle.isEmpty()) fileTitle = "No Video Loaded";

    juce::String techInfo = fileTitle + "  [" + juce::String(metadata.width) + "x" + juce::String(metadata.height)
                          + "  " + juce::String(metadata.frameRate, 2) + " fps  " + metadata.codecName + "]";

    g.setColour(Vep8Theme::textPrimary);
    g.setFont(Vep8Theme::getUiFont(11.5f, false));
    g.drawText(techInfo, topBar.reduced(10, 0), juce::Justification::centredLeft);

    // Right info: Sync state badge
    auto badgeArea = topBar.removeFromRight(180).reduced(4, 3);
    juce::Colour badgeColor = snapshot.isLockedToHost ? Vep8Theme::green : Vep8Theme::amber;
    juce::String badgeText = snapshot.isLockedToHost ? "LOCKED TO HOST" : "FREE SCRUB";

    g.setColour(badgeColor.withAlpha(0.2f));
    g.fillRoundedRectangle(badgeArea, 3.0f);
    g.setColour(badgeColor);
    g.drawRoundedRectangle(badgeArea, 3.0f, 1.0f);
    g.setFont(Vep8Theme::getUiFont(10.5f, true));
    g.drawText(badgeText, badgeArea, juce::Justification::centred);
}

void OverlayRenderer::drawBottomOsdHud(juce::Graphics& g, const juce::Rectangle<int>& videoArea, const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate, const CueMarkerModel& markerModel)
{
    float hudW = static_cast<float>(std::min(580, videoArea.getWidth() - 24));
    float hudH = 68.0f;
    juce::Rectangle<float> hudBox(static_cast<float>(videoArea.getCentreX()) - hudW * 0.5f,
                                 static_cast<float>(videoArea.getBottom()) - hudH - 12.0f,
                                 hudW, hudH);

    // Background glass plate
    g.setColour(Vep8Theme::bgMain.withAlpha(0.88f * osdOpacity_));
    g.fillRoundedRectangle(hudBox, 6.0f);
    g.setColour(Vep8Theme::borderLight.withAlpha(0.7f));
    g.drawRoundedRectangle(hudBox, 6.0f, 1.0f);

    auto contentArea = hudBox.reduced(10, 6);

    // Zone 1: SMPTE Timecode
    auto smpteZone = contentArea.removeFromLeft(contentArea.getWidth() * 0.44f);
    double targetPts = snapshot.getTargetVideoPts();
    SMPTETimecode tc = SMPTETimecode::fromSeconds(targetPts, smpteRate);

    g.setColour(Vep8Theme::textSecondary);
    g.setFont(Vep8Theme::getUiFont(10.0f, true));
    g.drawText("SMPTE TIMECODE (" + juce::String(getFrameRateLabel(smpteRate)) + ")",
               smpteZone.withHeight(14), juce::Justification::topLeft);

    g.setColour(Vep8Theme::amber);
    g.setFont(Vep8Theme::getMonospaceFont(22.0f, true));
    g.drawText(tc.toString(true), smpteZone.withTrimmedTop(14), juce::Justification::centredLeft);

    // Divider line
    g.setColour(Vep8Theme::border);
    g.fillRect(smpteZone.getRight() + 4.0f, hudBox.getY() + 8.0f, 1.0f, hudBox.getHeight() - 16.0f);

    // Zone 2: Musical Counter (Bar | Beat | Sub)
    auto musicalZone = contentArea.removeFromLeft(contentArea.getWidth() * 0.52f).withTrimmedLeft(12.0f);

    g.setColour(Vep8Theme::textSecondary);
    g.setFont(Vep8Theme::getUiFont(10.0f, true));
    juce::String tempoHeader = juce::String::formatted("BAR | BEAT | SUB (%.1f BPM)", snapshot.bpm);
    g.drawText(tempoHeader, musicalZone.withHeight(14), juce::Justification::topLeft);

    juce::String barBeatStr = TransportSyncEngine::formatMeasureBeat(snapshot.bar, snapshot.beat, snapshot.timeSigNumerator, snapshot.timeSigDenominator);
    g.setColour(Vep8Theme::cyan);
    g.setFont(Vep8Theme::getMonospaceFont(17.0f, true));
    g.drawText(barBeatStr, musicalZone.withTrimmedTop(14), juce::Justification::centredLeft);

    // Divider line
    g.setColour(Vep8Theme::border);
    g.fillRect(musicalZone.getRight() + 4.0f, hudBox.getY() + 8.0f, 1.0f, hudBox.getHeight() - 16.0f);

    // Zone 3: Active Cue & Next Cue Countdown
    auto cueZone = contentArea.withTrimmedLeft(12.0f);
    CueMarker currCue, nextCue;
    double countdown = -1.0;
    bool hasCueInfo = markerModel.getActiveCue(targetPts, snapshot.bpm, currCue, nextCue, countdown);

    g.setColour(Vep8Theme::textSecondary);
    g.setFont(Vep8Theme::getUiFont(10.0f, true));
    g.drawText("NEXT CUE READOUT", cueZone.withHeight(14), juce::Justification::topLeft);

    if (hasCueInfo && countdown >= 0.0)
    {
        juce::String countStr = juce::String::formatted("-%02.1fs", countdown);
        g.setColour(Vep8Theme::red);
        g.setFont(Vep8Theme::getMonospaceFont(14.0f, true));
        g.drawText(countStr + "  " + nextCue.name, cueZone.withTrimmedTop(14).withHeight(20), juce::Justification::centredLeft);

        g.setColour(Vep8Theme::textPrimary);
        g.setFont(Vep8Theme::getUiFont(10.5f, false));
        g.drawText(juce::String::formatted("Bar %d.%d", nextCue.bar, static_cast<int>(nextCue.beat)),
                   cueZone.withTrimmedTop(34), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(Vep8Theme::textDim);
        g.setFont(Vep8Theme::getUiFont(12.0f, false));
        g.drawText("No upcoming cue", cueZone.withTrimmedTop(14), juce::Justification::centredLeft);
    }
}

void OverlayRenderer::drawStreamers(juce::Graphics& g, const juce::Rectangle<int>& videoArea, double currentPts, double bpm, const CueMarkerModel& markerModel)
{
    auto markers = markerModel.getMarkersSorted();
    for (const auto& m : markers)
    {
        if (!m.streamerEnabled) continue;

        double hitPts = m.getEffectivePts(bpm, 4);
        double startPts = m.getStreamerStartPts(hitPts, bpm);
        double duration = hitPts - startPts;

        if (duration <= 0.0) continue;

        if (currentPts >= startPts && currentPts <= hitPts)
        {
            float progress = static_cast<float>((currentPts - startPts) / duration);
            float beamX = static_cast<float>(videoArea.getX()) + progress * static_cast<float>(videoArea.getWidth());

            float w = m.streamerWidth;

            // Glow aura
            if (m.streamerGlow)
            {
                g.setColour(m.color.withAlpha(0.2f * m.streamerOpacity));
                g.fillRect(beamX - w * 2.5f, static_cast<float>(videoArea.getY()), w * 5.0f, static_cast<float>(videoArea.getHeight()));

                g.setColour(m.color.withAlpha(0.4f * m.streamerOpacity));
                g.fillRect(beamX - w * 1.2f, static_cast<float>(videoArea.getY()), w * 2.4f, static_cast<float>(videoArea.getHeight()));
            }

            // Core beam
            g.setColour(m.color.withAlpha(m.streamerOpacity));
            g.fillRect(beamX - w * 0.5f, static_cast<float>(videoArea.getY()), w, static_cast<float>(videoArea.getHeight()));

            // Center white hot line
            g.setColour(juce::Colours::white.withAlpha(m.streamerOpacity));
            g.fillRect(beamX - 1.0f, static_cast<float>(videoArea.getY()), 2.0f, static_cast<float>(videoArea.getHeight()));

            // Top progress bar
            if (m.streamerTopBar)
            {
                float barH = 5.0f;
                g.setColour(m.color);
                g.fillRect(static_cast<float>(videoArea.getX()), static_cast<float>(videoArea.getY()),
                           beamX - static_cast<float>(videoArea.getX()), barH);
            }
        }
    }
}

void OverlayRenderer::drawPunchesAndFlutters(juce::Graphics& g, const juce::Rectangle<int>& videoArea, double currentPts, double frameRate, double bpm, const CueMarkerModel& markerModel)
{
    double frameDur = (frameRate > 0.0) ? (1.0 / frameRate) : 0.04;
    auto markers = markerModel.getMarkersSorted();

    for (const auto& m : markers)
    {
        double hitPts = m.getEffectivePts(bpm, 4);

        // 1. Punch Hit Flash
        if (m.punchEnabled)
        {
            double punchEndPts = hitPts + (m.punchFrameDuration * frameDur);
            if (currentPts >= hitPts && currentPts < punchEndPts)
            {
                float cx = static_cast<float>(videoArea.getCentreX());
                float cy = static_cast<float>(videoArea.getCentreY());
                float r = m.punchRadius;

                if (m.punchRingOnly)
                {
                    g.setColour(m.punchColor.withAlpha(0.9f));
                    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 6.0f);
                }
                else
                {
                    g.setColour(m.punchColor.withAlpha(0.85f));
                    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
                    g.setColour(juce::Colours::white);
                    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 3.0f);
                }
            }
        }

        // 2. Flutter warning sequence (3, 5, or 7 flashes leading to hitPts)
        if (m.flutterEnabled)
        {
            int count = m.flutterFlashCount;
            double flutterStart = hitPts - (count * 2 * frameDur);
            if (currentPts >= flutterStart && currentPts < hitPts)
            {
                int frameIdx = static_cast<int>(std::floor((currentPts - flutterStart) / frameDur));
                if (frameIdx % 2 == 0) // Flash on even frames
                {
                    float cx = static_cast<float>(videoArea.getCentreX());
                    float cy = static_cast<float>(videoArea.getCentreY());
                    float r = m.punchRadius * 0.7f;
                    g.setColour(Vep8Theme::amber.withAlpha(0.8f));
                    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 4.0f);
                }
            }
        }
    }
}

} // namespace MScoreVideoSync
