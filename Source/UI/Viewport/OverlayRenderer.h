#pragma once

#include "../../Core/CommonTypes.h"
#include "../../Cues/CueMarkerModel.h"
#include "../Theme/Vep8Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace MScoreVideoSync
{

class OverlayRenderer
{
public:
    OverlayRenderer();
    ~OverlayRenderer() = default;

    void render(juce::Graphics& g,
                const juce::Rectangle<int>& videoArea,
                const TransportSnapshot& snapshot,
                SMPTEFrameRate smpteRate,
                const VideoMetadata& metadata,
                const CueMarkerModel& markerModel);

    // OSD Configuration
    void setOsdVisible(bool visible) noexcept { showOsd_ = visible; }
    bool isOsdVisible() const noexcept { return showOsd_; }

    void setStreamersVisible(bool visible) noexcept { showStreamers_ = visible; }
    bool isStreamersVisible() const noexcept { return showStreamers_; }

    void setPunchesVisible(bool visible) noexcept { showPunches_ = visible; }
    bool isPunchesVisible() const noexcept { return showPunches_; }

    void setOsdOpacity(float opacity) noexcept { osdOpacity_ = juce::jlimit(0.0f, 1.0f, opacity); }
    float getOsdOpacity() const noexcept { return osdOpacity_; }

private:
    void drawTopMetadataBar(juce::Graphics& g, const juce::Rectangle<int>& videoArea, const VideoMetadata& metadata, const TransportSnapshot& snapshot);
    void drawBottomOsdHud(juce::Graphics& g, const juce::Rectangle<int>& videoArea, const TransportSnapshot& snapshot, SMPTEFrameRate smpteRate, const CueMarkerModel& markerModel);
    void drawStreamers(juce::Graphics& g, const juce::Rectangle<int>& videoArea, double currentPts, double bpm, const CueMarkerModel& markerModel);
    void drawPunchesAndFlutters(juce::Graphics& g, const juce::Rectangle<int>& videoArea, double currentPts, double frameRate, double bpm, const CueMarkerModel& markerModel);

    bool showOsd_{ true };
    bool showStreamers_{ true };
    bool showPunches_{ true };
    float osdOpacity_{ 0.85f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverlayRenderer)
};

} // namespace MScoreVideoSync
