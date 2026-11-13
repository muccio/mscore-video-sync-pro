#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <iomanip>
#include <sstream>

namespace MScoreVideoSync
{

// ==============================================================================
// SMPTE Frame Rates (SMPTE 12M standard)
// ==============================================================================
enum class SMPTEFrameRate : int
{
    FPS_23_976 = 0,
    FPS_24,
    FPS_25,
    FPS_29_97_DF,
    FPS_29_97_NDF,
    FPS_30,
    FPS_50,
    FPS_59_94_DF,
    FPS_59_94_NDF,
    FPS_60,
    Count
};

inline double getFrameRateValue(SMPTEFrameRate rate) noexcept
{
    switch (rate)
    {
        case SMPTEFrameRate::FPS_23_976:  return 24000.0 / 1001.0; // ~23.976023976
        case SMPTEFrameRate::FPS_24:      return 24.0;
        case SMPTEFrameRate::FPS_25:      return 25.0;
        case SMPTEFrameRate::FPS_29_97_DF:
        case SMPTEFrameRate::FPS_29_97_NDF: return 30000.0 / 1001.0; // ~29.97002997
        case SMPTEFrameRate::FPS_30:      return 30.0;
        case SMPTEFrameRate::FPS_50:      return 50.0;
        case SMPTEFrameRate::FPS_59_94_DF:
        case SMPTEFrameRate::FPS_59_94_NDF: return 60000.0 / 1001.0; // ~59.94005994
        case SMPTEFrameRate::FPS_60:      return 60.0;
        case SMPTEFrameRate::Count:
        default:                          return 25.0;
    }
}

inline const char* getFrameRateLabel(SMPTEFrameRate rate) noexcept
{
    switch (rate)
    {
        case SMPTEFrameRate::FPS_23_976:  return "23.976 fps";
        case SMPTEFrameRate::FPS_24:      return "24 fps";
        case SMPTEFrameRate::FPS_25:      return "25 fps";
        case SMPTEFrameRate::FPS_29_97_DF: return "29.97 fps (DF)";
        case SMPTEFrameRate::FPS_29_97_NDF: return "29.97 fps (NDF)";
        case SMPTEFrameRate::FPS_30:      return "30 fps";
        case SMPTEFrameRate::FPS_50:      return "50 fps";
        case SMPTEFrameRate::FPS_59_94_DF: return "59.94 fps (DF)";
        case SMPTEFrameRate::FPS_59_94_NDF: return "59.94 fps (NDF)";
        case SMPTEFrameRate::FPS_60:      return "60 fps";
        case SMPTEFrameRate::Count:
        default:                          return "25 fps";
    }
}

inline bool isDropFrame(SMPTEFrameRate rate) noexcept
{
    return rate == SMPTEFrameRate::FPS_29_97_DF || rate == SMPTEFrameRate::FPS_59_94_DF;
}

inline SMPTEFrameRate detectFrameRateFromFps(double fps) noexcept
{
    if (fps <= 0.0) return SMPTEFrameRate::FPS_24;
    if (std::abs(fps - (24000.0 / 1001.0)) < 0.05) return SMPTEFrameRate::FPS_23_976;
    if (std::abs(fps - 24.0) < 0.05) return SMPTEFrameRate::FPS_24;
    if (std::abs(fps - 25.0) < 0.05) return SMPTEFrameRate::FPS_25;
    if (std::abs(fps - (30000.0 / 1001.0)) < 0.05) return SMPTEFrameRate::FPS_29_97_NDF;
    if (std::abs(fps - 30.0) < 0.05) return SMPTEFrameRate::FPS_30;
    if (std::abs(fps - 50.0) < 0.05) return SMPTEFrameRate::FPS_50;
    if (std::abs(fps - (60000.0 / 1001.0)) < 0.05) return SMPTEFrameRate::FPS_59_94_NDF;
    if (std::abs(fps - 60.0) < 0.05) return SMPTEFrameRate::FPS_60;
    return SMPTEFrameRate::FPS_24;
}

// ==============================================================================
// SMPTE Timecode Representation
// ==============================================================================
struct SMPTETimecode
{
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frames = 0;
    int subframes = 0; // 0-99 (1/100th frame)
    bool dropFrame = false;
    bool isNegative = false;

    juce::String toString(bool showSubframes = false) const
    {
        char sep = dropFrame ? ';' : ':';
        juce::String prefix = isNegative ? "-" : "";
        if (showSubframes)
        {
            return juce::String::formatted("%s%02d:%02d:%02d%c%02d.%02d",
                                           prefix.toRawUTF8(),
                                           hours, minutes, seconds, sep, frames, subframes);
        }
        return juce::String::formatted("%s%02d:%02d:%02d%c%02d",
                                       prefix.toRawUTF8(),
                                       hours, minutes, seconds, sep, frames);
    }

    static SMPTETimecode fromSeconds(double totalSeconds, SMPTEFrameRate rate) noexcept
    {
        SMPTETimecode tc;
        if (std::isnan(totalSeconds) || std::isinf(totalSeconds))
            return tc;

        if (totalSeconds < 0.0)
        {
            tc.isNegative = true;
            totalSeconds = -totalSeconds;
        }

        tc.dropFrame = isDropFrame(rate);
        double fps = getFrameRateValue(rate);

        if (tc.dropFrame)
        {
            int dropFramesPerMin = (rate == SMPTEFrameRate::FPS_59_94_DF) ? 4 : 2;
            int framesPer10Min = (rate == SMPTEFrameRate::FPS_59_94_DF) ? 35964 : 17982;
            int framesPerMin = (rate == SMPTEFrameRate::FPS_59_94_DF) ? 3596 : 1798;
            int nominalFps = (rate == SMPTEFrameRate::FPS_59_94_DF) ? 60 : 30;

            int64_t totalFrameCount = static_cast<int64_t>(std::floor(totalSeconds * fps + 0.00001));
            
            int64_t d = totalFrameCount / framesPer10Min;
            int64_t m = totalFrameCount % framesPer10Min;

            int64_t frameAdjustment = 0;
            if (m > dropFramesPerMin)
            {
                frameAdjustment = dropFramesPerMin * (9 * d + (m - dropFramesPerMin) / framesPerMin);
            }
            else
            {
                frameAdjustment = dropFramesPerMin * 9 * d;
            }

            int64_t adjustedFrames = totalFrameCount + frameAdjustment;

            tc.frames = static_cast<int>(adjustedFrames % nominalFps);
            tc.seconds = static_cast<int>((adjustedFrames / nominalFps) % 60);
            tc.minutes = static_cast<int>((adjustedFrames / (nominalFps * 60)) % 60);
            tc.hours = static_cast<int>((adjustedFrames / (nominalFps * 3600)) % 24);

            double frameFrac = (totalSeconds * fps) - static_cast<double>(totalFrameCount);
            tc.subframes = juce::jlimit(0, 99, static_cast<int>(frameFrac * 100.0));
        }
        else
        {
            int64_t totalFrameCount = static_cast<int64_t>(std::floor(totalSeconds * fps + 0.00001));
            int nominalFps = static_cast<int>(std::round(fps));
            if (nominalFps <= 0) nominalFps = 25;

            tc.frames = static_cast<int>(totalFrameCount % nominalFps);
            int64_t totalSec = totalFrameCount / nominalFps;
            tc.seconds = static_cast<int>(totalSec % 60);
            tc.minutes = static_cast<int>((totalSec / 60) % 60);
            tc.hours = static_cast<int>((totalSec / 3600) % 24);

            double frameFrac = (totalSeconds * fps) - static_cast<double>(totalFrameCount);
            tc.subframes = juce::jlimit(0, 99, static_cast<int>(frameFrac * 100.0));
        }

        return tc;
    }

    double toSeconds(SMPTEFrameRate rate) const noexcept
    {
        double fps = getFrameRateValue(rate);
        double totalSec = 0.0;

        if (isDropFrame(rate))
        {
            int dropFramesPerMin = (rate == SMPTEFrameRate::FPS_59_94_DF) ? 4 : 2;
            int nominalFps = (rate == SMPTEFrameRate::FPS_59_94_DF) ? 60 : 30;

            int64_t totalMinutes = hours * 60 + minutes;
            int64_t droppedFrames = dropFramesPerMin * (totalMinutes - (totalMinutes / 10));
            int64_t frameCount = ((totalMinutes * 60 + seconds) * nominalFps + frames) - droppedFrames;

            totalSec = (static_cast<double>(frameCount) + (subframes / 100.0)) / fps;
        }
        else
        {
            int nominalFps = static_cast<int>(std::round(fps));
            int64_t frameCount = ((static_cast<int64_t>(hours) * 3600 + minutes * 60 + seconds) * nominalFps) + frames;
            totalSec = (static_cast<double>(frameCount) + (subframes / 100.0)) / fps;
        }

        return isNegative ? -totalSec : totalSec;
    }

    static SMPTETimecode parse(const juce::String& text, SMPTEFrameRate rate)
    {
        SMPTETimecode tc;
        auto tokens = juce::StringArray::fromTokens(text.trim(), ":;.", "");
        if (tokens.size() >= 4)
        {
            tc.hours = tokens[0].getIntValue();
            tc.minutes = tokens[1].getIntValue();
            tc.seconds = tokens[2].getIntValue();
            tc.frames = tokens[3].getIntValue();
            if (tokens.size() >= 5)
                tc.subframes = tokens[4].getIntValue();
            tc.dropFrame = isDropFrame(rate);
        }
        return tc;
    }
};

// ==============================================================================
// Synchronization Modes
// ==============================================================================
enum class SyncMode : int
{
    HostLockAutoScrub = 0, // Follows host in Play & Measure clicks; free-scrub in Stop; auto re-lock
    HostStrict,            // Strict slave to DAW playhead at all times
    FreeInternal,          // Independent internal transport (Local Play/Stop/Scrub)
    SatelliteIPCReceiver,  // Remote satellite client receiving telemetry from local shared memory/OSC
    Count
};

inline const char* getSyncModeLabel(SyncMode mode) noexcept
{
    switch (mode)
    {
        case SyncMode::HostLockAutoScrub:   return "Host Lock + Auto Scrub";
        case SyncMode::HostStrict:          return "Host Strict Lock";
        case SyncMode::FreeInternal:        return "Free Internal";
        case SyncMode::SatelliteIPCReceiver: return "Satellite IPC Receiver";
        case SyncMode::Count:
        default:                            return "Host Lock + Auto Scrub";
    }
}

// ==============================================================================
// Shared Memory IPC Structure (alignas(64) for Cache-Line Alignment)
// ==============================================================================
constexpr const char* IPC_SHARED_MEMORY_NAME = "/mscore_video_sync_shm";
constexpr uint32_t IPC_MAGIC_HEADER = 0x4D535653; // 'MSVS'
constexpr uint32_t IPC_CURRENT_VERSION = 1;

struct alignas(64) MScoreVideoSyncIPCHeader
{
    uint32_t magic = IPC_MAGIC_HEADER;
    uint32_t version = IPC_CURRENT_VERSION;
    std::atomic<uint64_t> sequenceCounter{ 0 };

    std::atomic<bool> isPlaying{ false };
    std::atomic<bool> isLooping{ false };
    std::atomic<double> sampleRate{ 44100.0 };
    std::atomic<int64_t> timeInSamples{ 0 };
    std::atomic<double> timeInSeconds{ 0.0 };
    std::atomic<double> ppqPosition{ 0.0 };
    std::atomic<double> bpm{ 120.0 };
    std::atomic<int32_t> timeSigNumerator{ 4 };
    std::atomic<int32_t> timeSigDenominator{ 4 };
    std::atomic<int32_t> barCount{ 1 };
    std::atomic<double> ppqPositionOfLastBarStart{ 0.0 };
    std::atomic<int64_t> hostTimeNs{ 0 };

    char videoFilePath[512] = { 0 };
    std::atomic<double> videoOffsetSeconds{ 0.0 };
    std::atomic<int32_t> smpteRate{ 0 };
    std::atomic<int32_t> syncMode{ 0 };
};

// ==============================================================================
// Transport Snapshot
// ==============================================================================
struct TransportSnapshot
{
    bool isPlaying = false;
    bool isRecording = false;
    bool isLooping = false;
    double timeInSeconds = 0.0;
    int64_t timeInSamples = 0;
    double bpm = 120.0;
    double ppqPosition = 0.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    int bar = 1;
    double beat = 1.0;
    double sampleRate = 44100.0;

    // Offsets & adjustments
    double videoOffsetSeconds = 0.0;
    double displayLatencySeconds = 0.0; // -0.5s to +0.5s
    double smpteStartTimeSeconds = 0.0; // e.g. 01:00:00:00 video start time
    double movieStartOffsetSeconds = 0.0; // Bar/Beat anchor offset

    SyncMode syncMode = SyncMode::HostLockAutoScrub;
    bool isLockedToHost = true;
    juce::String activeSource = "MuseScore 4";

    // Target video presentation timestamp (PTS) in seconds
    double getTargetVideoPts() const noexcept
    {
        double pts = timeInSeconds + videoOffsetSeconds + movieStartOffsetSeconds + displayLatencySeconds;
        return std::max(0.0, pts);
    }
};

// ==============================================================================
// Marker & Cue Types (VEP 8 Visual Cueing Engine)
// ==============================================================================
enum class MarkerAnchorType : int
{
    TimecodeLocked = 0, // Anchored to SMPTE timecode (fixed video frame)
    MusicalLocked        // Anchored to Musical Bar | Beat (follows tempo map / BPM)
};

enum class StreamerDurationType : int
{
    Seconds = 0, // Fixed time duration (1.0s, 1.5s, 2.0s, 3.0s)
    MusicalBeats // Musical duration (1 Bar, 2 Bars, 1 Beat, 2 Beats)
};

struct CueMarker
{
    int id = 0;
    juce::String name = "Marker";
    MarkerAnchorType anchorType = MarkerAnchorType::TimecodeLocked;

    // Timecode anchor (SMPTE / Video PTS)
    double timeSeconds = 0.0;

    // Musical anchor
    double ppqPosition = 0.0;
    int bar = 1;
    double beat = 1.0;

    juce::Colour color = juce::Colour::fromRGB(0, 210, 255); // Cyan default

    // Streamer settings (moving vertical preparation line)
    bool streamerEnabled = true;
    StreamerDurationType streamerDurationType = StreamerDurationType::Seconds;
    double streamerDurationSeconds = 2.0; // 2 seconds default
    double streamerDurationBeats = 4.0;   // 1 Bar in 4/4 default
    float streamerWidth = 6.0f;
    float streamerOpacity = 0.95f;
    bool streamerGlow = true;
    bool streamerTopBar = true;

    // Punch settings (visual hit flash)
    bool punchEnabled = true;
    int punchFrameDuration = 2; // 1-6 frames
    juce::Colour punchColor = juce::Colour::fromRGB(255, 255, 255); // White flash
    float punchRadius = 48.0f;
    bool punchRingOnly = false;

    // Flutter settings (rapid warning flashes)
    bool flutterEnabled = false;
    int flutterFlashCount = 5; // 3, 5, or 7 flashes

    // Audio cue (1000 Hz sine burst 2-Pop click)
    bool twoPopAudioEnabled = false;

    // Dynamic resolution based on tempo
    double getEffectivePts(double bpm, int /*timeSigNum*/ = 4) const noexcept
    {
        if (anchorType == MarkerAnchorType::TimecodeLocked)
            return timeSeconds;

        if (bpm <= 0.0) bpm = 120.0;
        return (ppqPosition * 60.0) / bpm;
    }

    double getStreamerStartPts(double effectivePts, double bpm) const noexcept
    {
        double durationSec = streamerDurationSeconds;
        if (streamerDurationType == StreamerDurationType::MusicalBeats)
        {
            if (bpm <= 0.0) bpm = 120.0;
            durationSec = (streamerDurationBeats * 60.0) / bpm;
        }
        return std::max(0.0, effectivePts - durationSec);
    }
};

// ==============================================================================
// Video File Metadata
// ==============================================================================
struct VideoMetadata
{
    bool isValid = false;
    juce::String filePath;
    int width = 0;
    int height = 0;
    double durationSeconds = 0.0;
    double frameRate = 0.0;
    juce::String codecName;
    bool isHardwareAccelerated = false;
    juce::String pixelFormatName;

    // Audio stream in video
    bool hasAudioStream = false;
    int audioSampleRate = 48000;
    int audioChannels = 2;
    juce::String audioCodecName;

    double getAspectRatio() const noexcept
    {
        return (height > 0) ? (static_cast<double>(width) / static_cast<double>(height)) : (16.0 / 9.0);
    }
};

// ==============================================================================
// Aspect Ratio Presets
// ==============================================================================
enum class AspectRatioMode : int
{
    Native = 0,
    Ratio_16_9,
    Ratio_2_39_1, // Anamorphic Cinemascope
    Ratio_1_85_1, // DCI / Theatrical Flat
    Ratio_4_3,
    FillStretch
};

// ==============================================================================
// Zoom Presets
// ==============================================================================
enum class VideoZoomPreset : int
{
    FitToScreen = 0,
    Zoom_50,
    Zoom_100,
    Zoom_150,
    Zoom_200
};

} // namespace MScoreVideoSync
