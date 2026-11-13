# MScore Video Sync Pro 🎬🎼

**Professional Vienna Ensemble Pro 8 (VEP 8) Architecture Video Synchronization & Film Scoring Platform for MuseScore 4 & DAWs (VST3, AU, Standalone).**

Inspired by the industry-standard **Vienna Ensemble Pro 8 (VEP 8) Video Pro Plugin**, `MScore Video Sync Pro` enables media composers, arrangers, and conductors to score directly in **MuseScore 4** (and professional DAWs) with hardware-accelerated, frame-accurate video playback, persistent floating viewports, multi-lane timelines, and visual cueing tools (Streamers, Punches, Flutters, and 2-Pop clicks).

---

## 🌟 Key Features (VEP 8 Video Pro Feature Set)

### 1. Hybrid Architecture & Persistent Window for MuseScore 4
* **Persistent Floating Video Window (In-Process)**:
  * MuseScore 4 terminates the GUI editor whenever the user closes the plugin from the mixer. `MScore Video Sync Pro` decouples the secondary floating video viewport from the editor lifecycle: the video window stays open, synchronized, and always on top even when closing the mixer plugin window.
  * **Always-on-Top**, **Borderless Mode** (pure video canvas + OSD without OS title bar), and **Click-Through Opacity Mode**.
* **Satellite / Bridge Mode (Fast Local IPC + Network OSC)**:
  * **POSIX Shared Memory IPC** (`/mscore_video_sync_shm`): 64-byte cache-line aligned lock-free atomic transport structure updated directly from the audio thread with zero locks and zero allocations.
  * **OSC over UDP Network Bridge**: dedicated worker thread broadcasting transport state at 60/120 Hz on port 9000, with bi-directional remote control on port 9001.
  * **Standalone Application**: `MScore Video Sync Pro.app` auto-discovers active MuseScore 4 sessions and locks to the transport instantaneously.

### 2. Frame-Accurate Synchronization & MuseScore 4 Quirks
* **Smart Handoff (Host Slave / Auto Free-Scrub)**:
  * **Slave to Host**: Follows score playback and measure selections with sample accuracy.
  * **Auto Free-Scrub in Stop**: When MuseScore is stopped, composers can freely scrub the timeline, use the mouse wheel or trackpad for step `-1f/+1f`, `-1s/+1s`, jump to markers, or play local previews.
  * **Instant Re-Lock**: As soon as MuseScore starts playing or the user clicks a note in the score, the plugin re-locks to the host automatically.
* **Frame Rate Engine**:
  * Automatic detection from video container (`23.976`, `24`, `25`, `29.97 DF/NDF`, `30`, `50`, `59.94 DF/NDF`, `60` fps) with manual override.
* **SMPTE Start Time & Musical Offset**:
  * Ancors the video start / FFOF to any SMPTE timecode (e.g. `01:00:00:00` or `00:59:52:00` for 2-pop leader) or any measure/beat in MuseScore.
  * Trim In / Trim Out handles to isolate scoring cues.
  * Fine Display Latency Compensation from `-500 ms` to `+500 ms` in 0.25 frame increments.

### 3. Hardware-Accelerated Decoding & Bidirectional Scrubbing Ring-Buffer
* **Hardware GPU Acceleration**:
  * Direct decoding via Apple VideoToolbox (`AV_HWDEVICE_TYPE_VIDEOTOOLBOX`) on macOS (Metal / CoreGraphics) with multi-threaded CPU fallback.
* **Bidirectional Scrubbing Ring-Buffer**:
  * Sliding window of cached decoded frames and keyframes around the playhead, guaranteeing smooth 60 fps scrubbing forward AND backward on Long-GOP (H.264/H.265) and Intra-frame codecs (Apple ProRes, DNxHR).
* **Asynchronous Timeline Tracks**:
  * **Video Filmstrip Track**: Background thumbnail extractor caching zoomable thumbnails along the timeline.
  * **Audio Waveform Track**: Background audio peak extractor generating high-contrast waveform displays for dialog and sound effects.

### 4. Visual Cueing System (Streamers, Punches, Flutters & 2-Pop Clicks)
* **Streamers (Hit Preparation Lines)**:
  * Glowing vertical beam traversing the screen from left to right, landing on the right margin exactly on the cue frame.
  * Configurable duration in seconds (`1.0s`, `1.5s`, `2.0s`, `3.0s`) or musical beats/bars (`1 Bar`, `2 Bars`, `1 Beat`, `2 Beats`) based on current BPM.
  * Custom RGB colors, width, opacity, glow aura, and top progress bar.
* **Punches & Flutters**:
  * **Punch (Hit Flash)**: High-contrast center circle or ring on hit frames for 1-6 frames duration.
  * **Flutter**: Rapid sequence of 3, 5, or 7 warning flashes for tight rhythmic cues.
  * **Audible 2-Pop / Cue Click**: Lock-free real-time DSP sine wave generator (1000 Hz, 1 frame, cosine anti-click envelope) mixed directly into the audio output.
* **Dual-Anchored Marker Manager**:
  * **Timecode-Locked Marker**: Anchored to a fixed SMPTE video frame (e.g., scene cuts, SFX hitpoints).
  * **Musical-Locked Marker**: Anchored to score measures/beats (e.g., downbeats, tempo transitions).
  * Marker table inspector with instant jump on double click.
  * Universal import/export: CSV Cue Sheets, JSON (`.mvsp`), and Standard MIDI (`.mid`) tempo maps from MuseScore 4.

### 5. Multi-Zone On-Screen Display (OSD HUD) & VEP 8 UI
* **VEP 8 Studio Dark Theme**:
  * Professional studio dark palette (`#121417`, `#1B1F24`, `#262B33`, `#00D2FF` cyan, `#FFB000` amber, `#FF3B30` red).
  * Strict tabular monospace typography for jitter-free timecode and measure display.
* **Multi-Zone OSD HUD**:
  * SMPTE Counter (`HH:MM:SS:FF.ss`), FPS badge, absolute frame counter.
  * Musical Counter (`BAR | BEAT | SUB`, BPM, Time Signature).
  * Active Cue Readout & Next Cue Countdown (`-02.4s Cue 3 (m.12.1)`).
* **Multi-Lane Timeline**:
  * Dual Ruler (SMPTE + Bars/Beats).
  * Marker Lane with colored flags and streamer duration shading.
  * Filmstrip Thumbnail Lane.
  * Embedded Audio Waveform Lane.
  * Audio Strip with Volume Gain (`-inf` to `+12 dB`), Mute, Dim (`-15 dB`), and stereo peak level meter.

### 6. Embedded Audio Engine & Project Recall
* Decodes embedded video audio via FFmpeg and resamples to host sample rate with `libswresample` into a lock-free circular FIFO (`juce::AbstractFifo`) with 5ms micro-fades.
* Full state persistence in DAW sessions and standalone `.mvsp` JSON files.

---

## 🏗 Modular Architecture

```
mscore-video-sync-pro/
├── CMakeLists.txt (VST3, AU, Standalone, juce_osc, libswresample)
├── Source/
│   ├── Core/
│   │   ├── CommonTypes.h          (SMPTE, DropFrame, Anchors, IPC data structures)
│   │   ├── PluginProcessor.h/.cpp (AudioProcessor, DSP, floating window lifecycle)
│   │   └── ProjectState.h/.cpp    (ValueTree & .mvsp JSON project serialization)
│   ├── Sync/
│   │   ├── TransportSyncEngine.h/.cpp (Host playhead tracking, MuseScore quirks, Smart Handoff)
│   │   ├── SharedMemoryIPC.h/.cpp     (POSIX shm_open + mmap lock-free 64-byte aligned IPC)
│   │   └── NetworkBridge.h/.cpp       (OSC over UDP transport broadcasting & remote control)
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
│       │   └── TimelineComponent.h/.cpp   (Dual rulers, Marker lane, Filmstrip, Waveform)
│       ├── Inspector/
│       │   └── MarkerInspector.h/.cpp     (Drawer cue table, marker properties, cue countdown)
│       └── PluginEditor.h/.cpp            (Integrated control room layout & detach switch)
```

---

## 🚀 Build & Installation

### 1. Prerequisites (macOS)
```bash
brew install cmake ninja pkg-config ffmpeg
```

### 2. Compile Everything (VST3, AU, Standalone)
```bash
./scripts/build_mac.sh
```

Or configure manually via CMake:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

The build produces:
* **VST3 Plugin**: `build/MScoreVideoSyncPro_artefacts/Release/VST3/MScore Video Sync Pro.vst3` (automatically copied to `~/Library/Audio/Plug-Ins/VST3/`)
* **Audio Unit Plugin**: `build/MScoreVideoSyncPro_artefacts/Release/AU/MScore Video Sync Pro.component`
* **Standalone Application**: `build/MScoreVideoSyncPro_artefacts/Release/Standalone/MScore Video Sync Pro.app`

---

## 🎼 Scoring Workflow with MuseScore 4

1. Open **MuseScore 4**.
2. Press `F10` to open the **Mixer**.
3. Under the Master channel or an Instrument channel, click an effect slot and insert **MScore Video Sync Pro**.
4. Drag & drop your video file directly onto the plugin window or click **Load Video...**.
5. Click **Float Window** in the top bar to detach the persistent video viewport:
   * You can now close the plugin window in MuseScore's mixer.
   * The detached floating video viewport will remain active, always on top, and fully synchronized with the score!
6. Select any note or measure in MuseScore to instantly seek the video to that exact frame.
7. Press `M` or click **+ Cue** at any time to place hitpoints with streamers and punches.
8. Export/Import your cues to CSV or import tempo maps from MuseScore MIDI files (`.mid`).
