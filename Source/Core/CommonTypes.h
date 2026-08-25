#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace MScoreVideoSync
{

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
            // Drop Frame SMPTE calculation (SMPTE 12M-1)
            // For 29.97 DF: 108,000 frames in 1 hour; drop 2 frames every minute except 10th minute
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
};

enum class SyncMode : int
{
    DAWPlayHead = 0, // Default: Host DAW PlayHead & sample tracking (MuseScore 4 projectTimeSamples, Cubase, Logic, Reaper)
    Auto,            // Automatic best-fit hybrid
    AudioStream,     // Sample-accurate audio clock: advances PTS when audio blocks stream through mixer
    MIDIClock,       // MIDI timing clock and song position pointer
    Manual,          // Internal manual transport only
    Count
};

inline const char* getSyncModeLabel(SyncMode mode) noexcept
{
    switch (mode)
    {
        case SyncMode::DAWPlayHead: return "DAW PlayHead (Default)";
        case SyncMode::Auto:        return "Auto (Hybrid)";
        case SyncMode::AudioStream: return "Audio Stream Clock";
        case SyncMode::MIDIClock:   return "MIDI Clock / Events";
        case SyncMode::Manual:      return "Manual Only";
        case SyncMode::Count:
        default:                    return "DAW PlayHead";
    }
}

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
    double sampleRate = 44100.0;
    double videoOffsetSeconds = 0.0;

    SyncMode syncMode = SyncMode::DAWPlayHead;
    juce::String activeSource = "None";
    float audioLevel = 0.0f;
    bool midiActive = false;

    // Derived target video presentation timestamp (PTS) in seconds
    double getTargetVideoPts() const noexcept
    {
        return std::max(0.0, timeInSeconds + videoOffsetSeconds);
    }
};

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

    double getAspectRatio() const noexcept
    {
        return (height > 0) ? (static_cast<double>(width) / static_cast<double>(height)) : (16.0 / 9.0);
    }
};

} // namespace MScoreVideoSync
