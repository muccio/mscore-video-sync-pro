# IMPLEMENTATION PLAN: MScore Video Sync Pro (VEP 8 Architecture Evolution)

## Executive Summary
This document outlines the architectural transformation of `mscore-video-sync-pro` into an industry-grade video synchronization and film scoring platform inspired by the **Vienna Ensemble Pro 8 (VEP 8) Video Pro Plugin**, tailored specifically for **MuseScore 4** and professional DAWs (VST3, AU, and Standalone).

---

## 1. Architectural Baseline & Audit

### 1.1 Current Architecture Analysis
* **Build System (`CMakeLists.txt`)**: JUCE 8.0.6 via FetchContent, FFmpeg (libavcodec, libavformat, libswscale, libavutil) via Homebrew, macOS frameworks (VideoToolbox, CoreMedia, CoreVideo, AudioToolbox, OpenGL, Cocoa, Carbon). Plugin formats currently built: VST3, AU.
* **Core Modules**:
  * `Source/Core/CommonTypes.h`: SMPTE calculation (DF/NDF), basic `TransportSnapshot`, `VideoMetadata`.
  * `Source/Core/TempoMapTracker`: Basic playhead tracking and fallback watchdog.
  * `Source/Core/PluginProcessor`: AudioProcessor lifecycle, XML/ValueTree persistence.
  * `Source/Video/LockFreeFrameQueue`: Triple-buffered frame queue for decoder-to-UI handoff.
  * `Source/Video/FFmpegDecoder`: Threaded decoder with VideoToolbox hardware acceleration and software fallback.
  * `Source/UI/VideoViewport`: Basic video frame drawing and rudimentary HUD.
  * `Source/UI/PluginEditor`: In-window UI with basic buttons, sliders, and 60Hz timer.

### 1.2 Identified Architectural Deficiencies vs VEP 8 Video Pro
1. **Window Destruction by MuseScore 4**: MuseScore 4 terminates the `AudioProcessorEditor` instance whenever the user closes the plugin GUI from the mixer. The current codebase ties the video canvas directly to the editor, causing complete loss of video display during scoring.
2. **Missing Satellite / Bridge Mode**: No inter-process communication (IPC) or network broadcast mechanism to drive a separate Standalone Video Pro display or external monitors/iPads.
3. **Transport Quirks in MuseScore 4**: Discontinuous playhead jumps when clicking notes/measures in Stop mode are not smoothly differentiated from scrubbing or transport playback.
4. **No Visual Cueing Engine**: Lacks streamers, punches, flutters, audible 2-pop cue clicks, and dual-anchored markers (SMPTE vs Musical Bar/Beat).
5. **No Multi-Lane Timeline**: Lacks filmstrip thumbnails, audio waveform display, and dual SMPTE/Musical rulers.
6. **No Embedded Video Audio Output**: Audio from the video file is not decoded, resampled, or routed to the host mixer.
7. **Single-Window UI**: Lacks floating window detachment, borderless mode, always-on-top, click-through opacity, and aspect ratio lock.

---

## 2. Target VEP 8 Architecture & Module Map

```
mscore-video-sync-pro/
├── CMakeLists.txt (VST3, AU, Standalone, juce_osc, libswresample)
├── Source/
│   ├── Core/
│   │   ├── CommonTypes.h          (SMPTE, DropFrame, Formats, Anchors, RingBuffers)
│   │   ├── PluginProcessor.h/.cpp (AudioProcessor, state recall, floating window manager)
│   │   └── ProjectState.h/.cpp    (JSON / XML full state serialization & .mvsp export)
│   ├── Sync/
│   │   ├── TransportSyncEngine.h/.cpp (PlayHead tracking, MuseScore quirks, Smart Handoff)
│   │   ├── SharedMemoryIPC.h/.cpp     (POSIX shm_open + mmap, lock-free 64-byte aligned IPC)
│   │   └── NetworkBridge.h/.cpp       (OSC over UDP via juce_osc + WebSocket telemetry)
│   ├── Video/
│   │   ├── VideoDecodeEngine.h/.cpp   (Hardware VideoToolbox/FFmpeg + predictive ring buffer)
│   │   ├── LockFreeFrameQueue.h       (SPSC triple buffer & ring cache)
│   │   ├── FilmstripGenerator.h/.cpp  (Asynchronous background thumbnail extractor)
│   │   └── WaveformExtractor.h/.cpp   (Asynchronous embedded audio peak extractor)
│   ├── Audio/
│   │   ├── EmbeddedAudioPlayer.h/.cpp (FFmpeg audio decode, libswresample, lock-free FIFO)
│   │   └── TwoPopGenerator.h/.cpp     (DSP 1kHz sine burst cue click with anti-click envelope)
│   ├── Cues/
│   │   ├── CueMarkerModel.h/.cpp      (SMPTE/Musical dual anchor, Streamers, Punches, Flutters)
│   │   └── MidiMarkerImporter.h/.cpp  (Standard MIDI tempo map & cue sheet import/export)
│   └── UI/
│       ├── Theme/
│       │   └── Vep8Theme.h            (Studio Dark Palette, Tabular Monospace Fonts)
│       ├── Viewport/
│       │   ├── VideoViewport.h/.cpp   (Retina OpenGL/Metal canvas, aspect ratio lock)
│       │   └── OverlayRenderer.h/.cpp (Multi-zone OSD, Streamers beam, Punch/Flutter flash)
│       ├── Window/
│       │   └── FloatingVideoWindow.h/.cpp (Persistent In-Process, Borderless, Always-on-Top)
│       ├── Timeline/
│       │   ├── TimelineComponent.h/.cpp   (Dual rulers, Marker lane, Filmstrip, Waveform)
│       │   └── TransportBar.h/.cpp        (Scrubbing, Step -/+1f, -/+1s, Audio gain/meter)
│       ├── Inspector/
│       │   └── MarkerInspector.h/.cpp     (Drawer cue table, marker properties, cue countdown)
│       └── PluginEditor.h/.cpp            (Integrated control room layout & detach switch)
```

---

## 3. Evolutionary Implementation Phases

### Phase 2: Hybrid Architecture & Window Persistence for MuseScore 4
* **Step 2.1**: Implement `FloatingVideoWindow` decoupling from `PluginEditor`. The window is owned by `MScoreVideoSyncProcessor` and survives editor destruction.
* **Step 2.2**: Add Borderless mode, Always-on-Top toggle, and Click-Through Opacity mode.
* **Step 2.3**: Implement `SharedMemoryIPC` with POSIX `shm_open`, `mmap`, and `alignas(64)` lock-free atomic transport data structure.
* **Step 2.4**: Implement `NetworkBridge` using `juce_osc` for 60/120 Hz UDP transport broadcasting and command reception.
* **Step 2.5**: Enable Standalone application target in CMake (`FORMATS VST3 AU Standalone`) with auto-discovery of active plugin IPC stream.

### Phase 3: Frame-Accurate Sync Engine & MuseScore 4 Quirks
* **Step 3.1**: Build `TransportSyncEngine` to handle discontinuous playhead jumps during Stop mode note selections in MuseScore 4.
* **Step 3.2**: Implement Smart Handoff (Slave-to-Host in Play/Seek, Auto Free-Scrub in Stop, instant re-lock on host play).
* **Step 3.3**: Implement complete Frame Rate Engine (auto-detection from video stream + manual override for 23.976, 24, 25, 29.97 DF/NDF, 30, 50, 59.94 DF/NDF, 60 fps).
* **Step 3.4**: Implement SMPTE Start Time (e.g. `01:00:00:00`), Musical Offset (Bar/Beat anchor), and Trim In/Out bounds.
* **Step 3.5**: Add Display Latency Compensation (-500ms to +500ms in 0.25 frame increments).

### Phase 4: Hardware-Accelerated Decoding, Cache & Timeline Tracks
* **Step 4.1**: Upgrade `VideoDecodeEngine` with a bidirectional scrubbing ring-buffer around the playhead for instant 60 fps scrubbing in Long-GOP (H.264/H.265) and Intra-frame (ProRes, DNxHR).
* **Step 4.2**: Implement `FilmstripGenerator` background worker to extract and cache video thumbnails across the timeline.
* **Step 4.3**: Implement `WaveformExtractor` background worker to decode audio peaks and render high-contrast waveforms.

### Phase 5: Visual Cueing System (Streamers, Punches, Flutters & Markers)
* **Step 5.1**: Implement `CueMarkerModel` with dual anchoring (Timecode-locked vs Musical-locked).
* **Step 5.2**: Implement `OverlayRenderer` for GPU-accelerated vector streamers (customizable duration in seconds or beats, colors, glow, top progress bar).
* **Step 5.3**: Implement Punches (1-6 frames circle/ring or border flash) and Flutters (3, 5, 7 flashes).
* **Step 5.4**: Implement `TwoPopGenerator` lock-free 1000 Hz sine burst cue click synth in the audio processor block.
* **Step 5.5**: Add JSON, CSV, and MIDI (`.mid`) tempo map & marker import/export.

### Phase 6: OSD & UI (Vienna Ensemble Pro 8 Design)
* **Step 6.1**: Implement `Vep8Theme` with studio dark styling (`#121417`, `#1B1F24`, `#262B33`, `#00D2FF`, `#FFB000`, `#FF3B30`) and monospace tabular typography.
* **Step 6.2**: Implement Multi-zone OSD HUD (SMPTE counter, Frame counter, Musical Bar|Beat|Sub, BPM, Time Sig, Cue name & Next Cue countdown).
* **Step 6.3**: Implement Top Control Bar with Load Video, recent files, Sync Mode LED, SMPTE start, bar/beat offset, latency, FPS badge, and quick toggles.
* **Step 6.4**: Implement Bottom Transport & Multi-Lane Timeline (dual rulers, marker lane, filmstrip lane, waveform lane, transport controls, audio controls).
* **Step 6.5**: Implement collapsible `MarkerInspector` cue table drawer.

### Phase 7: Embedded Audio Engine & State Serialization
* **Step 7.1**: Implement `EmbeddedAudioPlayer` with `libavcodec` and `libswresample` to decode and resample video audio to host sample rate.
* **Step 7.2**: Implement lock-free audio ring buffer with 5ms micro-fades and mix with 2-pop into plugin output bus.
* **Step 7.3**: Implement complete state persistence in `getStateInformation`/`setStateInformation` and external `.mvsp` JSON project files.

### Phase 8: Verification, Compilation & Performance Validation
* **Step 8.1**: Comprehensive CMake configuration & compilation test for VST3, AU, and Standalone.
* **Step 8.2**: Real-time audio thread verification (zero allocations, zero locks in `processBlock`).
* **Step 8.3**: Verification of window persistence when closing editor in MuseScore 4.
* **Step 8.4**: Verification of shared memory IPC and OSC synchronization.
