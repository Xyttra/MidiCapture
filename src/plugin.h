#pragma once

#include <cstdint>
#include <cstring>
#include <atomic>
#include <clap/clap.h>

// A single recorded MIDI event with timestamp
struct TimestampedMidiEvent {
    double   time_seconds;  // absolute time from plugin activation
    uint8_t  data[3];
    uint8_t  size;          // 1, 2, or 3
};

// Ring buffer capacity: 10 minutes at ~1000 events/sec = 600,000
// We'll use 1M for safety
static constexpr uint32_t RING_BUFFER_CAPACITY = 1024 * 1024;

class MidiCapture {
public:
    MidiCapture(const clap_host_t* host);
    ~MidiCapture();

    // CLAP lifecycle
    bool init();
    void destroy();
    bool activate(double sample_rate, uint32_t min_frames, uint32_t max_frames);
    void deactivate();
    bool startProcessing();
    void stopProcessing();
    void reset();

    // CLAP process
    clap_process_status process(const clap_process_t* process);

    // Extension queries
    const void* getExtension(const char* id);
    void onMainThread();

    // Ring buffer access (called from GUI thread)
    // Returns the number of events copied into `out`, up to `max_count`.
    // Events are returned in chronological order, oldest first.
    uint32_t snapshotEvents(TimestampedMidiEvent* out, uint32_t max_count) const;

    // Get current buffer duration in seconds
    double getBufferDuration() const;

    const clap_host_t* _host;
    const clap_plugin_t* _plugin; // set by entry.cpp after creation

    struct GuiState* _guiState = nullptr;

private:
    // Ring buffer (lock-free: single producer audio thread, single consumer GUI thread)
    TimestampedMidiEvent _ring[RING_BUFFER_CAPACITY];
    std::atomic<uint32_t> _writeHead{0};
    uint32_t _eventCount{0}; // total events written (saturates at RING_BUFFER_CAPACITY)

    double _sampleRate{48000.0};
    double _startTime{0.0};   // steady_time at activation
    bool   _activated{false};

    // State saving
    bool save(const clap_ostream_t* stream);
    bool load(const clap_istream_t* stream);

    // Note ports extension
    static uint32_t notePortsCount(const clap_plugin_t* plugin, bool is_input);
    static bool notePortsGet(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_note_port_info_t* info);
    static const clap_plugin_note_ports_t _notePortsExtension;

    // State extension
    static bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream);
    static bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream);
    static const clap_plugin_state_t _stateExtension;
};
