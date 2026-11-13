#pragma once

#include "CueMarkerModel.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace MScoreVideoSync
{

class MidiMarkerImporter
{
public:
    struct ImportResult
    {
        bool success = false;
        int markersImported = 0;
        double initialBpm = 120.0;
        int timeSigNum = 4;
        int timeSigDenom = 4;
        juce::String errorMessage;
    };

    static ImportResult importFromMidiFile(const juce::File& midiFile, CueMarkerModel& markerModel);
};

} // namespace MScoreVideoSync
