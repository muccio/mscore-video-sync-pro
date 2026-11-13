#include "ProjectState.h"

namespace MScoreVideoSync
{

juce::ValueTree ProjectStateSerializer::toValueTree(const SerializedPluginState& s)
{
    juce::ValueTree vt("MScoreVideoSyncProject");

    vt.setProperty("videoFilePath", s.videoFilePath, nullptr);
    vt.setProperty("videoOffsetSeconds", s.videoOffsetSeconds, nullptr);
    vt.setProperty("smpteStartTimeSeconds", s.smpteStartTimeSeconds, nullptr);
    vt.setProperty("movieStartOffsetSeconds", s.movieStartOffsetSeconds, nullptr);
    vt.setProperty("displayLatencySeconds", s.displayLatencySeconds, nullptr);
    vt.setProperty("trimInSeconds", s.trimInSeconds, nullptr);
    vt.setProperty("trimOutSeconds", s.trimOutSeconds, nullptr);

    vt.setProperty("smpteRate", s.smpteRate, nullptr);
    vt.setProperty("syncMode", s.syncMode, nullptr);

    vt.setProperty("audioGainDb", s.audioGainDb, nullptr);
    vt.setProperty("isAudioMuted", s.isAudioMuted, nullptr);
    vt.setProperty("isAudioDimmed", s.isAudioDimmed, nullptr);

    vt.setProperty("showOsd", s.showOsd, nullptr);
    vt.setProperty("showStreamers", s.showStreamers, nullptr);
    vt.setProperty("showPunches", s.showPunches, nullptr);
    vt.setProperty("osdOpacity", s.osdOpacity, nullptr);
    vt.setProperty("aspectRatioMode", s.aspectRatioMode, nullptr);
    vt.setProperty("zoomPreset", s.zoomPreset, nullptr);

    vt.setProperty("isFloatingWindowVisible", s.isFloatingWindowVisible, nullptr);
    vt.setProperty("floatingWindowGeometry", s.floatingWindowGeometry, nullptr);
    vt.setProperty("isAlwaysOnTop", s.isAlwaysOnTop, nullptr);
    vt.setProperty("isBorderless", s.isBorderless, nullptr);
    vt.setProperty("isClickThrough", s.isClickThrough, nullptr);

    if (!s.markersJson.isVoid())
    {
        vt.setProperty("markersJson", juce::JSON::toString(s.markersJson), nullptr);
    }

    return vt;
}

SerializedPluginState ProjectStateSerializer::fromValueTree(const juce::ValueTree& vt)
{
    SerializedPluginState s;
    if (!vt.isValid()) return s;

    s.videoFilePath = vt.getProperty("videoFilePath", "").toString();
    s.videoOffsetSeconds = vt.getProperty("videoOffsetSeconds", 0.0);
    s.smpteStartTimeSeconds = vt.getProperty("smpteStartTimeSeconds", 0.0);
    s.movieStartOffsetSeconds = vt.getProperty("movieStartOffsetSeconds", 0.0);
    s.displayLatencySeconds = vt.getProperty("displayLatencySeconds", 0.0);
    s.trimInSeconds = vt.getProperty("trimInSeconds", 0.0);
    s.trimOutSeconds = vt.getProperty("trimOutSeconds", 0.0);

    s.smpteRate = vt.getProperty("smpteRate", static_cast<int>(SMPTEFrameRate::FPS_24));
    s.syncMode = vt.getProperty("syncMode", static_cast<int>(SyncMode::HostLockAutoScrub));

    s.audioGainDb = static_cast<float>(vt.getProperty("audioGainDb", 0.0f));
    s.isAudioMuted = vt.getProperty("isAudioMuted", false);
    s.isAudioDimmed = vt.getProperty("isAudioDimmed", false);

    s.showOsd = vt.getProperty("showOsd", true);
    s.showStreamers = vt.getProperty("showStreamers", true);
    s.showPunches = vt.getProperty("showPunches", true);
    s.osdOpacity = static_cast<float>(vt.getProperty("osdOpacity", 0.85f));
    s.aspectRatioMode = vt.getProperty("aspectRatioMode", 0);
    s.zoomPreset = vt.getProperty("zoomPreset", 0);

    s.isFloatingWindowVisible = vt.getProperty("isFloatingWindowVisible", false);
    s.floatingWindowGeometry = vt.getProperty("floatingWindowGeometry", "").toString();
    s.isAlwaysOnTop = vt.getProperty("isAlwaysOnTop", true);
    s.isBorderless = vt.getProperty("isBorderless", false);
    s.isClickThrough = vt.getProperty("isClickThrough", false);

    juce::String jsonStr = vt.getProperty("markersJson", "").toString();
    if (jsonStr.isNotEmpty())
    {
        s.markersJson = juce::JSON::parse(jsonStr);
    }

    return s;
}

juce::String ProjectStateSerializer::toJsonString(const SerializedPluginState& s)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("videoFilePath", s.videoFilePath);
    root->setProperty("videoOffsetSeconds", s.videoOffsetSeconds);
    root->setProperty("smpteStartTimeSeconds", s.smpteStartTimeSeconds);
    root->setProperty("movieStartOffsetSeconds", s.movieStartOffsetSeconds);
    root->setProperty("displayLatencySeconds", s.displayLatencySeconds);
    root->setProperty("trimInSeconds", s.trimInSeconds);
    root->setProperty("trimOutSeconds", s.trimOutSeconds);

    root->setProperty("smpteRate", s.smpteRate);
    root->setProperty("syncMode", s.syncMode);

    root->setProperty("audioGainDb", s.audioGainDb);
    root->setProperty("isAudioMuted", s.isAudioMuted);
    root->setProperty("isAudioDimmed", s.isAudioDimmed);

    root->setProperty("showOsd", s.showOsd);
    root->setProperty("showStreamers", s.showStreamers);
    root->setProperty("showPunches", s.showPunches);
    root->setProperty("osdOpacity", s.osdOpacity);
    root->setProperty("aspectRatioMode", s.aspectRatioMode);
    root->setProperty("zoomPreset", s.zoomPreset);

    root->setProperty("isFloatingWindowVisible", s.isFloatingWindowVisible);
    root->setProperty("floatingWindowGeometry", s.floatingWindowGeometry);
    root->setProperty("isAlwaysOnTop", s.isAlwaysOnTop);
    root->setProperty("isBorderless", s.isBorderless);
    root->setProperty("isClickThrough", s.isClickThrough);

    if (!s.markersJson.isVoid())
    {
        root->setProperty("markers", s.markersJson);
    }

    return juce::JSON::toString(juce::var(root), true);
}

SerializedPluginState ProjectStateSerializer::fromJsonString(const juce::String& jsonString)
{
    SerializedPluginState s;
    auto parsed = juce::JSON::parse(jsonString);
    if (!parsed.isObject()) return s;

    s.videoFilePath = parsed.getProperty("videoFilePath", "").toString();
    s.videoOffsetSeconds = parsed.getProperty("videoOffsetSeconds", 0.0);
    s.smpteStartTimeSeconds = parsed.getProperty("smpteStartTimeSeconds", 0.0);
    s.movieStartOffsetSeconds = parsed.getProperty("movieStartOffsetSeconds", 0.0);
    s.displayLatencySeconds = parsed.getProperty("displayLatencySeconds", 0.0);
    s.trimInSeconds = parsed.getProperty("trimInSeconds", 0.0);
    s.trimOutSeconds = parsed.getProperty("trimOutSeconds", 0.0);

    s.smpteRate = parsed.getProperty("smpteRate", static_cast<int>(SMPTEFrameRate::FPS_24));
    s.syncMode = parsed.getProperty("syncMode", static_cast<int>(SyncMode::HostLockAutoScrub));

    s.audioGainDb = static_cast<float>(parsed.getProperty("audioGainDb", 0.0f));
    s.isAudioMuted = parsed.getProperty("isAudioMuted", false);
    s.isAudioDimmed = parsed.getProperty("isAudioDimmed", false);

    s.showOsd = parsed.getProperty("showOsd", true);
    s.showStreamers = parsed.getProperty("showStreamers", true);
    s.showPunches = parsed.getProperty("showPunches", true);
    s.osdOpacity = static_cast<float>(parsed.getProperty("osdOpacity", 0.85f));
    s.aspectRatioMode = parsed.getProperty("aspectRatioMode", 0);
    s.zoomPreset = parsed.getProperty("zoomPreset", 0);

    s.isFloatingWindowVisible = parsed.getProperty("isFloatingWindowVisible", false);
    s.floatingWindowGeometry = parsed.getProperty("floatingWindowGeometry", "").toString();
    s.isAlwaysOnTop = parsed.getProperty("isAlwaysOnTop", true);
    s.isBorderless = parsed.getProperty("isBorderless", false);
    s.isClickThrough = parsed.getProperty("isClickThrough", false);

    if (parsed.hasProperty("markers"))
    {
        s.markersJson = parsed.getProperty("markers", juce::var());
    }

    return s;
}

bool ProjectStateSerializer::saveToMvspFile(const juce::File& file, const SerializedPluginState& state)
{
    return file.replaceWithText(toJsonString(state));
}

bool ProjectStateSerializer::loadFromMvspFile(const juce::File& file, SerializedPluginState& outState)
{
    if (!file.existsAsFile()) return false;
    outState = fromJsonString(file.loadFileAsString());
    return true;
}

} // namespace MScoreVideoSync
