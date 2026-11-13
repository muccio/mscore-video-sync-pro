#pragma once

#include "../../Core/CommonTypes.h"
#include "../../Cues/CueMarkerModel.h"
#include "../../Cues/MidiMarkerImporter.h"
#include "../Theme/Vep8Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace MScoreVideoSync
{

class MarkerInspector : public juce::Component,
                        public juce::TableListBoxModel,
                        public CueMarkerModel::Listener
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void locateToMarker(double timeSeconds) = 0;
    };

    MarkerInspector(CueMarkerModel& markerModel, SMPTEFrameRate smpteRate);
    ~MarkerInspector() override;

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    void setSmpteFrameRate(SMPTEFrameRate rate);

    // juce::TableListBoxModel overrides
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;

    // CueMarkerModel::Listener
    void markersChanged() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void openMidiImportDialog();
    void openCsvExportDialog();
    void openCsvImportDialog();

    CueMarkerModel& markerModel_;
    SMPTEFrameRate smpteRate_{ SMPTEFrameRate::FPS_24 };

    juce::TableListBox table_;
    juce::TextButton btnAddMarker_{ "+ Add" };
    juce::TextButton btnDeleteMarker_{ "- Del" };
    juce::TextButton btnImportMidi_{ "MIDI In" };
    juce::TextButton btnExportCsv_{ "CSV Out" };
    juce::TextButton btnImportCsv_{ "CSV In" };

    std::vector<CueMarker> cachedMarkers_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerInspector)
};

} // namespace MScoreVideoSync
