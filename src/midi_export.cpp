#include "midi_export.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

// ============================================================
// Standard MIDI File Writer (Format 0, single track)
// No dependencies — raw binary output
// ============================================================

namespace MidiExport {

// Write a big-endian 16-bit value
static void write16(FILE* f, uint16_t val) {
    uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    fwrite(buf, 1, 2, f);
}

// Write a big-endian 32-bit value
static void write32(FILE* f, uint32_t val) {
    uint8_t buf[4] = {
        (uint8_t)(val >> 24), (uint8_t)(val >> 16),
        (uint8_t)(val >> 8),  (uint8_t)(val & 0xFF)
    };
    fwrite(buf, 1, 4, f);
}

// Write a MIDI variable-length quantity
static void writeVarLen(FILE* f, uint32_t val) {
    uint8_t buf[4];
    int n = 0;
    buf[n++] = val & 0x7F;
    while (val >>= 7) {
        buf[n++] = (val & 0x7F) | 0x80;
    }
    // Reverse order
    for (int i = n - 1; i >= 0; --i) {
        fwrite(&buf[i], 1, 1, f);
    }
}

bool writeFile(const char* path, const TimestampedMidiEvent* events, uint32_t count) {
    if (count == 0) return false;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    // We'll use 480 ticks per quarter note, assume 120 BPM
    const uint16_t ticksPerQN = 480;
    const double bpm = 120.0;
    const double ticksPerSecond = (bpm / 60.0) * ticksPerQN;

    // Find the earliest timestamp to normalize
    double baseTime = events[0].time_seconds;

    // Convert events to tick-timestamped MIDI bytes in a buffer
    struct MidiEvent {
        uint32_t tick;
        uint8_t data[3];
        uint8_t size;
    };

    std::vector<MidiEvent> midiEvents;
    midiEvents.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        MidiEvent me;
        double relTime = events[i].time_seconds - baseTime;
        if (relTime < 0.0) relTime = 0.0;
        me.tick = (uint32_t)(relTime * ticksPerSecond);
        memcpy(me.data, events[i].data, 3);
        me.size = events[i].size;
        midiEvents.push_back(me);
    }

    // Sort by tick (should already be sorted, but be safe)
    std::sort(midiEvents.begin(), midiEvents.end(),
        [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    // Build the track data buffer
    std::vector<uint8_t> trackData;

    // Set tempo meta event: FF 51 03 <microseconds per quarter note>
    {
        uint32_t usPerQN = (uint32_t)(60000000.0 / bpm);
        trackData.push_back(0x00); // delta time = 0
        trackData.push_back(0xFF);
        trackData.push_back(0x51);
        trackData.push_back(0x03);
        trackData.push_back((usPerQN >> 16) & 0xFF);
        trackData.push_back((usPerQN >> 8) & 0xFF);
        trackData.push_back(usPerQN & 0xFF);
    }

    // Write MIDI events with delta times
    uint32_t prevTick = 0;
    for (const auto& me : midiEvents) {
        uint32_t delta = me.tick - prevTick;
        prevTick = me.tick;

        // Variable-length delta time
        uint8_t vlBuf[4];
        int vlLen = 0;
        vlBuf[vlLen++] = delta & 0x7F;
        uint32_t tmp = delta;
        while (tmp >>= 7) {
            vlBuf[vlLen++] = (tmp & 0x7F) | 0x80;
        }
        for (int i = vlLen - 1; i >= 0; --i) {
            trackData.push_back(vlBuf[i]);
        }

        // MIDI data
        for (uint8_t j = 0; j < me.size; ++j) {
            trackData.push_back(me.data[j]);
        }
    }

    // End of track meta event: FF 2F 00
    trackData.push_back(0x00); // delta = 0
    trackData.push_back(0xFF);
    trackData.push_back(0x2F);
    trackData.push_back(0x00);

    // Write the file
    // MThd header
    fwrite("MThd", 1, 4, f);
    write32(f, 6);             // header length
    write16(f, 0);             // format 0
    write16(f, 1);             // 1 track
    write16(f, ticksPerQN);    // ticks per QN

    // MTrk header
    fwrite("MTrk", 1, 4, f);
    write32(f, (uint32_t)trackData.size());
    fwrite(trackData.data(), 1, trackData.size(), f);

    fclose(f);
    return true;
}

} // namespace MidiExport
