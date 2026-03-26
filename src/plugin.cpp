#include "plugin.h"
#include "gui.h"
#include <cstring>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <ole2.h>
#endif

// ============================================================
// Note Ports Extension
// ============================================================

const clap_plugin_note_ports_t MidiCapture::_notePortsExtension = {
    MidiCapture::notePortsCount,
    MidiCapture::notePortsGet,
};

uint32_t MidiCapture::notePortsCount(const clap_plugin_t* plugin, bool is_input) {
    return is_input ? 1 : 0; // 1 MIDI input, no output
}

bool MidiCapture::notePortsGet(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_note_port_info_t* info) {
    if (!is_input || index != 0) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    strncpy(info->name, "MIDI In", CLAP_NAME_SIZE);
    return true;
}

// ============================================================
// Plugin Lifecycle
// ============================================================

MidiCapture::MidiCapture(const clap_host_t* host)
    : _host(host), _plugin(nullptr) {
    memset(_ring, 0, sizeof(_ring));
}

MidiCapture::~MidiCapture() {}

bool MidiCapture::init() {
#ifdef _WIN32
    OleInitialize(nullptr);
#endif
    return true;
}

void MidiCapture::destroy() {
#ifdef _WIN32
    OleUninitialize();
#endif
    delete this;
}

bool MidiCapture::activate(double sample_rate, uint32_t /*min_frames*/, uint32_t /*max_frames*/) {
    _sampleRate = sample_rate;
    _activated = true;
    _startTime = 0.0;
    _writeHead.store(0);
    _eventCount = 0;
    return true;
}

void MidiCapture::deactivate() {
    _activated = false;
}

bool MidiCapture::startProcessing() {
    return true;
}

void MidiCapture::stopProcessing() {}

void MidiCapture::reset() {
    _writeHead.store(0);
    _eventCount = 0;
}

// ============================================================
// Process — capture ALL incoming MIDI events
// ============================================================

clap_process_status MidiCapture::process(const clap_process_t* proc) {
    if (!proc->in_events) return CLAP_PROCESS_CONTINUE;

    const uint32_t eventCount = proc->in_events->size(proc->in_events);

    // Calculate block start time in seconds
    double blockStartSeconds = 0.0;
    if (proc->steady_time >= 0) {
        blockStartSeconds = double(proc->steady_time) / _sampleRate;
        if (_startTime == 0.0) _startTime = blockStartSeconds;
    }

    for (uint32_t i = 0; i < eventCount; ++i) {
        const clap_event_header_t* hdr = proc->in_events->get(proc->in_events, i);

        if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        TimestampedMidiEvent ev;
        ev.size = 0;

        if (hdr->type == CLAP_EVENT_MIDI) {
            // Raw MIDI event
            const auto* midi = reinterpret_cast<const clap_event_midi_t*>(hdr);
            ev.data[0] = midi->data[0];
            ev.data[1] = midi->data[1];
            ev.data[2] = midi->data[2];
            ev.size = 3;
        } else if (hdr->type == CLAP_EVENT_NOTE_ON) {
            // CLAP note on -> convert to MIDI note on
            const auto* note = reinterpret_cast<const clap_event_note_t*>(hdr);
            uint8_t channel = (uint8_t)(note->channel & 0x0F);
            uint8_t key = (uint8_t)(note->key & 0x7F);
            uint8_t vel = (uint8_t)(note->velocity * 127.0);
            if (vel == 0) vel = 1; // MIDI note on vel=0 means note off
            ev.data[0] = 0x90 | channel;
            ev.data[1] = key;
            ev.data[2] = vel;
            ev.size = 3;
        } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
            // CLAP note off -> convert to MIDI note off
            const auto* note = reinterpret_cast<const clap_event_note_t*>(hdr);
            uint8_t channel = (uint8_t)(note->channel & 0x0F);
            uint8_t key = (uint8_t)(note->key & 0x7F);
            uint8_t vel = (uint8_t)(note->velocity * 127.0);
            ev.data[0] = 0x80 | channel;
            ev.data[1] = key;
            ev.data[2] = vel;
            ev.size = 3;
        }

        if (ev.size > 0) {
            // Calculate precise timestamp
            double sampleOffset = double(hdr->time) / _sampleRate;
            ev.time_seconds = (blockStartSeconds - _startTime) + sampleOffset;

            uint32_t pos = _writeHead.load(std::memory_order_relaxed);
            _ring[pos] = ev;
            _writeHead.store((pos + 1) % RING_BUFFER_CAPACITY, std::memory_order_release);

            if (_eventCount < RING_BUFFER_CAPACITY)
                _eventCount++;
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

// ============================================================
// Extension Queries
// ============================================================

const void* MidiCapture::getExtension(const char* id) {
    if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &_notePortsExtension;
    if (strcmp(id, CLAP_EXT_GUI) == 0)
        return MidiCaptureGui::getExtension();
    if (strcmp(id, CLAP_EXT_STATE) == 0)
        return &_stateExtension;
    return nullptr;
}

void MidiCapture::onMainThread() {}

// ============================================================
// State Saving / Loading
// ============================================================

const clap_plugin_state_t MidiCapture::_stateExtension = {
    MidiCapture::stateSave,
    MidiCapture::stateLoad,
};

bool MidiCapture::stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    return static_cast<MidiCapture*>(plugin->plugin_data)->save(stream);
}

bool MidiCapture::stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    return static_cast<MidiCapture*>(plugin->plugin_data)->load(stream);
}

bool MidiCapture::save(const clap_ostream_t* stream) {
    static constexpr uint32_t MAX_SAVE = RING_BUFFER_CAPACITY;
    auto* events = new TimestampedMidiEvent[MAX_SAVE];
    uint32_t count = snapshotEvents(events, MAX_SAVE);

    uint32_t version = 1;
    stream->write(stream, &version, sizeof(version));
    stream->write(stream, &count, sizeof(count));

    if (count > 0) {
        stream->write(stream, events, count * sizeof(TimestampedMidiEvent));
    }

    delete[] events;
    return true;
}

bool MidiCapture::load(const clap_istream_t* stream) {
    uint32_t version = 0;
    if (stream->read(stream, &version, sizeof(version)) != sizeof(version)) return false;
    if (version != 1) return false;

    uint32_t count = 0;
    if (stream->read(stream, &count, sizeof(count)) != sizeof(count)) return false;

    _writeHead.store(0);
    _eventCount = 0;

    if (count > 0) {
        uint32_t toRead = std::min(count, RING_BUFFER_CAPACITY);
        if (stream->read(stream, _ring, toRead * sizeof(TimestampedMidiEvent)) == (int64_t)(toRead * sizeof(TimestampedMidiEvent))) {
            _writeHead.store(toRead % RING_BUFFER_CAPACITY);
            _eventCount = toRead;
        }
        
        if (count > RING_BUFFER_CAPACITY) {
            uint8_t dummy[sizeof(TimestampedMidiEvent)];
            for (uint32_t i = 0; i < count - RING_BUFFER_CAPACITY; ++i) {
                stream->read(stream, dummy, sizeof(dummy));
            }
        }
    }

    return true;
}

// ============================================================
// Ring Buffer Snapshot (called from GUI/main thread)
// ============================================================

uint32_t MidiCapture::snapshotEvents(TimestampedMidiEvent* out, uint32_t max_count) const {
    uint32_t head = _writeHead.load(std::memory_order_acquire);
    uint32_t count = std::min(_eventCount, max_count);
    count = std::min(count, RING_BUFFER_CAPACITY);

    if (count == 0) return 0;

    uint32_t start;
    if (_eventCount >= RING_BUFFER_CAPACITY) {
        start = head;
    } else {
        start = 0;
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < count && written < max_count; ++i) {
        uint32_t idx = (start + i) % RING_BUFFER_CAPACITY;
        if (_ring[idx].size > 0) {
            out[written++] = _ring[idx];
        }
    }

    return written;
}

double MidiCapture::getBufferDuration() const {
    if (_eventCount == 0) return 0.0;
    uint32_t head = _writeHead.load(std::memory_order_acquire);
    uint32_t start;
    if (_eventCount >= RING_BUFFER_CAPACITY)
        start = head;
    else
        start = 0;

    double oldest = _ring[start].time_seconds;
    double newest = _ring[(head + RING_BUFFER_CAPACITY - 1) % RING_BUFFER_CAPACITY].time_seconds;
    return newest - oldest;
}
