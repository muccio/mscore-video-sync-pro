# MScore Video Sync Pro 🎬🎼

**Ultra-low-latency, frame-accurate VST3 / AU video score player for macOS running inside MuseScore 4.x & DAWs.**

`MScore Video Sync Pro` enables film and media composers to score directly inside **MuseScore 4** with hardware-accelerated video playback synchronized to the score playhead and note selection events.

---

## Key Features

- **Apple VideoToolbox Hardware Acceleration**:
  - Direct hardware-accelerated H.264, HEVC (H.265), and Apple ProRes decoding using `AV_HWDEVICE_TYPE_VIDEOTOOLBOX`.
  - Automatic fallback to high-efficiency multithreaded software decoding (`swscale`).
- **MuseScore 4 Host Synchronization**:
  - Instant transport tracking via `juce::AudioPlayHead` in `processBlock`.
  - In `DAW PlayHead` mode, selecting any note or measure in MuseScore immediately seeks the video frame to that exact timestamp.
  - 100% Real-Time safe audio thread execution: zero memory allocations, no lock contention, and no Objective-C ARC overhead.
- **Lock-Free Triple Buffer Video Engine**:
  - Asynchronous background decoder thread decouples video demuxing/decoding from the UI render loop.
  - Zero frame dropping and smooth 60 Hz Retina rendering via CoreGraphics or OpenGL.
- **Professional Scoring HUD & SMPTE Engine**:
  - Broadcast-standard SMPTE timecode display (`HH:MM:SS:FF.ss`) supporting 23.976, 24, 25, 29.97 (DF/NDF), 30, 50, 59.94, and 60 fps.
  - Live Frame Counter and sub-millisecond precision PTS seconds.
- **Interactive Offset & Transport Control**:
  - Sub-millisecond video offset slider (-30.0s to +30.0s) for precision audio-to-picture alignment.
  - Integrated timeline scrubber with live timecode readout and smooth play/pause preview.

---

## Architecture Overview

```
                          ┌────────────────────────┐
                          │   MuseScore 4 Host     │
                          │ (CoreAudio processBlock│
                          └───────────┬────────────┘
                                      │ Lock-free Atomics
                                      ▼
                          ┌────────────────────────┐
                          │   PluginProcessor      │
                          │   TempoMapTracker      │
                          └───────────┬────────────┘
                                      │ Target PTS & Seek Triggers
                                      ▼
┌───────────────────────┐ ◄───────────┴────────────► ┌──────────────────────┐
│  FFmpegDecoder Thread │                            │   UI Render (60Hz)   │
│ ┌───────────────────┐ │                            │ ┌──────────────────┐ │
│ │ VideoToolbox HW   │ │                            │ │ VideoViewport    │ │
│ │ AVHWDeviceContext │ │   Lock-Free Triple Buffer  │ │ Retina / OpenGL  │ │
│ └─────────┬─────────┘ │ ─────────────────────────► │ ├──────────────────┤ │
│           ▼           │   (LockFreeFrameQueue)     │ │ SMPTE HUD Glass  │ │
│ ┌───────────────────┐ │                            │ ├──────────────────┤ │
│ │ SwsContext (ARGB) │ │                            │ │ Transport Bar    │ │
│ └───────────────────┘ │                            │ └──────────────────┘ │
└───────────────────────┘                            └──────────────────────┘
```

---

## Requirements

- **macOS**: 12.0 (Monterey) or newer (Apple Silicon `arm64` & Intel `x86_64`).
- **FFmpeg**: 6.x, 7.x, or 8.x (installed via Homebrew: `brew install ffmpeg`).
- **MuseScore**: 4.0 or newer with VST3 plugin support.
- **Build Tools**: `cmake`, `ninja`, `pkg-config`, and Xcode Command Line Tools.

---

## Quickstart: Build & Install

### 1. Install Prerequisites via Homebrew
```bash
brew install cmake ninja pkg-config ffmpeg
```

### 2. Automated 1-Click Build & Install
Run the provided build script from the repository root:
```bash
./scripts/build_mac.sh
```

To build a **Universal Binary** (`arm64` and `x86_64`):
```bash
./scripts/build_mac.sh --universal
```

The script will:
1. Fetch and configure JUCE 8 via CMake `FetchContent`.
2. Locate FFmpeg and macOS native frameworks (`VideoToolbox`, `CoreMedia`, `CoreVideo`, `OpenGL`).
3. Compile the optimized Release VST3 bundle using Ninja.
4. Install `MScore Video Sync Pro.vst3` into `~/Library/Audio/Plug-Ins/VST3/`.

---

## Using in MuseScore 4

1. Open **MuseScore 4**.
2. Open the **Mixer** (`F10`) or add an **Instrument Track**.
3. Insert **MScore Video Sync Pro** on the track.
4. In the plugin window:
   - Click **📂 Load Video...** (or drag and drop any `.mov`, `.mp4`, `.mkv`, or `.prores` file into the viewport).
   - Use the **Video Offset** slider to adjust score-to-picture sync if needed.
   - Set your desired **SMPTE FPS** rate (e.g. 24, 25, 29.97, 30).
5. Start playback or click note events in your score: the video immediately displays the matching frame in real time!

---

## File Structure

```
.
├── CMakeLists.txt              # CMake configuration (macOS Universal Binary & JUCE 8)
├── README.md                   # Documentation & setup guide
├── docs/
│   └── plans/
│       └── task.md             # Task progress tracker
├── scripts/
│   └── build_mac.sh            # Automated compilation and deployment script
└── Source/
    ├── Core/
    │   ├── CommonTypes.h       # SMPTE timecode math, transport snapshot
    │   ├── TempoMapTracker.h   # Real-time sample/PTS tracking & sync modes
    │   ├── TempoMapTracker.cpp
    │   ├── PluginProcessor.h   # Real-time CoreAudio processor & state saving
    │   └── PluginProcessor.cpp
    ├── Video/
    │   ├── LockFreeFrameQueue.h# Zero-latency lock-free triple buffer
    │   ├── FFmpegDecoder.h     # VideoToolbox hardware-accelerated decoder
    │   └── FFmpegDecoder.cpp
    └── UI/
        ├── VideoViewport.h     # Retina video display & HUD overlay
        ├── VideoViewport.cpp
        ├── PluginEditor.h      # Clean GUI, 60Hz timer & transport scrubber
        └── PluginEditor.cpp
```

---

## License
MIT License. Built with JUCE and FFmpeg.
