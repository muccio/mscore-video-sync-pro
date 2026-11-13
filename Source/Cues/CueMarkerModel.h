#pragma once

#include "../Core/CommonTypes.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <mutex>

namespace MScoreVideoSync
{

class CueMarkerModel
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void markersChanged() = 0;
    };

    CueMarkerModel();
    ~CueMarkerModel() = default;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    // Marker manipulation
    int addMarker(const CueMarker& marker);
    bool removeMarker(int markerId);
    void clearMarkers();
    bool updateMarker(const CueMarker& marker);

    // Quick add marker at specific playhead time
    int addQuickMarker(double timeSeconds, double ppq, int bar, double beat, const juce::String& name = {});

    // Retrieval
    std::vector<CueMarker> getMarkersSorted() const;
    bool getMarkerById(int markerId, CueMarker& outMarker) const;
    int getNumMarkers() const;

    // Nearest / active markers
    bool getActiveCue(double currentPts, double bpm, CueMarker& outCurrentCue, CueMarker& outNextCue, double& outCountdownSec) const;

    // Tempo map update: recalculates complementary positions based on anchor type
    void updateTempoMap(double bpm, int timeSigNum, int timeSigDenom);

    // Import / Export
    juce::var exportToJson() const;
    bool importFromJson(const juce::var& json);

    juce::String exportToCsv(SMPTEFrameRate smpteRate) const;
    bool importFromCsv(const juce::String& csvData, SMPTEFrameRate smpteRate);

private:
    void notifyListeners();
    void sortMarkersInternal();

    std::vector<CueMarker> markers_;
    mutable std::mutex markersMutex_;
    int nextMarkerId_{ 1 };

    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CueMarkerModel)
};

} // namespace MScoreVideoSync
