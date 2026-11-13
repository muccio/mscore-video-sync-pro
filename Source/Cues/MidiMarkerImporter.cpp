#include "MidiMarkerImporter.h"

namespace MScoreVideoSync
{

MidiMarkerImporter::ImportResult MidiMarkerImporter::importFromMidiFile(const juce::File& midiFile, CueMarkerModel& markerModel)
{
    ImportResult result;
    if (!midiFile.existsAsFile())
    {
        result.errorMessage = "MIDI file does not exist: " + midiFile.getFullPathName();
        return result;
    }

    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
    {
        result.errorMessage = "Failed to open MIDI stream";
        return result;
    }

    juce::MidiFile mf;
    if (!mf.readFrom(stream))
    {
        result.errorMessage = "Failed to parse Standard MIDI file format";
        return result;
    }

    short timeFormat = mf.getTimeFormat();
    double ticksPerQuarter = (timeFormat > 0) ? static_cast<double>(timeFormat) : 480.0;

    int numTracks = mf.getNumTracks();
    double currentBpm = 120.0;
    int sigNum = 4;
    int sigDen = 4;

    // Scan for initial tempo & time signature
    for (int t = 0; t < numTracks; ++t)
    {
        const auto* track = mf.getTrack(t);
        if (track == nullptr) continue;

        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            auto* holder = track->getEventPointer(i);
            const auto& msg = holder->message;

            if (msg.isTempoMetaEvent())
            {
                currentBpm = msg.getTempoSecondsPerQuarterNote() > 0.0 ? (60.0 / msg.getTempoSecondsPerQuarterNote()) : 120.0;
                result.initialBpm = currentBpm;
            }
            else if (msg.isTimeSignatureMetaEvent())
            {
                msg.getTimeSignatureInfo(sigNum, sigDen);
                result.timeSigNum = sigNum;
                result.timeSigDenom = sigDen;
            }
        }
    }

    // Now scan for Marker and Cue events
    int count = 0;
    for (int t = 0; t < numTracks; ++t)
    {
        const auto* track = mf.getTrack(t);
        if (track == nullptr) continue;

        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            auto* holder = track->getEventPointer(i);
            const auto& msg = holder->message;

            juce::String markerText;
            bool isMarker = false;

            if (msg.isMetaEvent())
            {
                const uint8_t* raw = msg.getRawData();
                int rawLen = msg.getRawDataSize();
                if (rawLen >= 3 && raw[0] == 0xFF)
                {
                    uint8_t type = raw[1];
                    // 0x06: Marker, 0x07: Cue point, 0x01: Text event
                    if (type == 0x06 || type == 0x07 || type == 0x03)
                    {
                        markerText = msg.getTextFromTextMetaEvent();
                        isMarker = true;
                    }
                }
            }

            if (isMarker && markerText.isNotEmpty())
            {
                double tick = holder->message.getTimeStamp();
                double ppq = tick / ticksPerQuarter;
                double seconds = (ppq * 60.0) / currentBpm;

                CueMarker m;
                m.name = markerText;
                m.timeSeconds = seconds;
                m.ppqPosition = ppq;
                m.anchorType = MarkerAnchorType::MusicalLocked;

                double beatsPerBar = static_cast<double>(sigNum) * (4.0 / static_cast<double>(sigDen));
                m.bar = static_cast<int>(std::floor(ppq / beatsPerBar)) + 1;
                m.beat = 1.0 + std::fmod(ppq, beatsPerBar);

                markerModel.addMarker(m);
                count++;
            }
        }
    }

    result.markersImported = count;
    result.success = true;
    return result;
}

} // namespace MScoreVideoSync
