#pragma once

#include "CommonTypes.h"
#include "../Cues/CueMarkerModel.h"
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace MScoreVideoSync
{

struct SerializedPluginState
{
    juce::String videoFilePath;
    double videoOffsetSeconds = 0.0;
    double smpteStartTimeSeconds = 0.0;
    double movieStartOffsetSeconds = 0.0;
    double displayLatencySeconds = 0.0;
    double trimInSeconds = 0.0;
    double trimOutSeconds = 0.0;

    int smpteRate = static_cast<int>(SMPTEFrameRate::FPS_24);
    int syncMode = static_cast<int>(SyncMode::HostLockAutoScrub);

    // Audio
    float audioGainDb = 0.0f;
    bool isAudioMuted = false;
    bool isAudioDimmed = false;

    // Viewport & HUD
    bool showOsd = true;
    bool showStreamers = true;
    bool showPunches = true;
    float osdOpacity = 0.85f;
    int aspectRatioMode = 0;
    int zoomPreset = 0;

    // Floating Window
    bool isFloatingWindowVisible = false;
    juce::String floatingWindowGeometry;
    bool isAlwaysOnTop = true;
    bool isBorderless = false;
    bool isClickThrough = false;

    // Markers
    juce::var markersJson;
};

class ProjectStateSerializer
{
public:
    static juce::ValueTree toValueTree(const SerializedPluginState& state);
    static SerializedPluginState fromValueTree(const juce::ValueTree& vt);

    static juce::String toJsonString(const SerializedPluginState& state);
    static SerializedPluginState fromJsonString(const juce::String& jsonString);

    static bool saveToMvspFile(const juce::File& file, const SerializedPluginState& state);
    static bool loadFromMvspFile(const juce::File& file, SerializedPluginState& outState);
};

} // namespace MScoreVideoSync
