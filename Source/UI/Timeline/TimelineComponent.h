#pragma once

#include "../../Core/CommonTypes.h"
#include "../../Sync/TransportSyncEngine.h"
#include "../../Video/VideoDecodeEngine.h"
#include "../../Cues/CueMarkerModel.h"
#include "../Theme/Vep8Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace MScoreVideoSync
{

class TimelineComponent : public juce::Component,
                          public CueMarkerModel::Listener,
                          public FilmstripGenerator::Listener,
                          public WaveformExtractor::Listener
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void timelineScrubbed(double targetPts) = 0;
        virtual void markerSelected(int markerId) = 0;
    };

    TimelineComponent(TransportSyncEngine& syncEngine,
                      VideoDecodeEngine& decoder,
                      CueMarkerModel& markerModel);
    ~TimelineComponent() override;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    void updateSnapshot(const TransportSnapshot& snapshot, SMPTEFrameRate rate);

    // Zoom & View Range
    void setZoomFactor(double factor);
    double getZoomFactor() const noexcept { return zoomFactor_; }

    // Listeners from models
    void markersChanged() override;
    void filmstripUpdated() override;
    void waveformReady() override;

private:
    double xToTime(float x) const noexcept;
    float timeToX(double time) const noexcept;

    void drawDualRuler(juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawMarkerLane(juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawFilmstripLane(juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawWaveformLane(juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawPlayhead(juce::Graphics& g, const juce::Rectangle<float>& area);

    TransportSyncEngine& syncEngine_;
    VideoDecodeEngine& decoder_;
    CueMarkerModel& markerModel_;

    TransportSnapshot currentSnapshot_;
    SMPTEFrameRate currentSmpteRate_{ SMPTEFrameRate::FPS_24 };

    double viewStartSec_{ 0.0 };
    double viewDurationSec_{ 60.0 };
    double zoomFactor_{ 1.0 }; // 1.0 = normal, 2.0 = 2x zoom in

    bool isDraggingPlayhead_{ false };
    int draggingMarkerId_{ -1 };

    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};

} // namespace MScoreVideoSync
