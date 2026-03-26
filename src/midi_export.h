#pragma once

#include "plugin.h"

namespace MidiExport {
    // Write a Standard MIDI File (Format 0, single track) from the given events.
    // Returns true on success.
    bool writeFile(const char* path, const TimestampedMidiEvent* events, uint32_t count);
}
