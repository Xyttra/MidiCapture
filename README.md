# MidiCapture

MidiCapture is a lightweight CLAP (Clever Audio Plug-in API) audio/MIDI plugin designed to capture incoming MIDI events from a DAW (Digital Audio Workstation) and export them as a standard MIDI file. It is especially useful for capturing MIDI Polyphonic Expression (MPE) data, improvisations, or generating MIDI out of procedural/generative MIDI effects without requiring a dedicated DAW recording track. It has been primarily developed for Windows (using Win32 and GDI+).

## Features

- **CLAP Compatibility**: Built utilizing the standard CLAP C-API.
- **Lock-Free MIDI Capture**: Captures MIDI stream data on the audio processing thread using a 1,000,000 event capacity lock-free ring buffer.
- **MPE Support**: Accurately tracks and exports MPE expressive events (timbre, channel pressure, pitch bend) per note.
- **Drag-and-Drop Export**: Writes standard MIDI files (Format 0) to disk from captured performances.
- **Native Windows UI**: Uses simple, raw Win32 APIs for its interface—keeping the plugin lightweight and preventing large multi-framework dependencies.

## Architecture & Code Structure

The source tree is structured as follows:

```
src/
├── entry.cpp         # CLAP entry point, factory definition, and plugin instance registration.
├── plugin.h/cpp      # Core plugin logic, lifecycle management, audio-thread MIDI event ring buffering, and state handling.
├── gui.h/cpp         # Windows native interface (win32 API/GDI+), handling the cross-thread updates from the audio buffer to the UI.
└── midi_export.h/cpp # MIDI File format (SMF) generator. Reads timestamped captured data and serializes into a standard .mid file.
```

### Key Technical Details
1. **The Ring Buffer**: In `plugin.h`, you'll notice `_ring[RING_BUFFER_CAPACITY]`. The audio thread (managed in `process()`) operates as a single producer writing to this lock-free circular buffer. The GUI thread occasionally reads snapshots of these timestamped events.
2. **CLAP GUI Extension**: The plugin advertises the `clap_plugin_gui_t` extension in `gui.cpp`, relying exclusively on Win32 HWNDs for rendering rather than a framework like JUCE.
3. **MIDI Export**: Format 0 standard MIDI files are serialized out in `midi_export.cpp`, properly sorting events by ticks/deltas calculated from `TimestampedMidiEvent` absolute time fields.

## Building from Source

### Requirements
- A system running **Windows** (due to direct Win32 API interactions).
- CMake 3.22 or higher.
- A C++17 compatible compiler (e.g., MSVC, GCC, or Clang).

The CLAP SDK dependency is resolved automatically via CMake `FetchContent` (pointing directly to the GitHub release).

### Build Instructions

From the root project directory, run:

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

On a successful build, a post-build command copies the resulting `MidiCapture.clap` plugin to your local user CLAP directory (`%LOCALAPPDATA%\Programs\Common\CLAP`), effectively automatically installing the plugin for your local DAWs.

## Usage

1. Load `MidiCapture` as a MIDI effect/instrument on a track in a CLAP-compatible DAW (e.g., Bitwig Studio, REAPER).
2. Route any MIDI (or MPE) source to the plugin.
3. Improvisation or playback events are automatically cached into the internal ring buffer.
4. Export the performance from the GUI to a `.mid` file based on the captured stream!

## License

(Add the appropriate license here)
