// toolboxes/audio/src/io/midi_codec.hpp
//
// In-tree Standard MIDI File (SMF 0/1) reader, writer, and peeker.
// Zero-dependency pure C++17 implementation.

#pragma once

#include <keel/array.h>
#include <keel/core.h>
#include <keel/memory.h>
#include <keel/ref.h>
#include <keel/string.h>
#include <cstdint>
#include <numkit/value/value.hpp>

namespace numkit::audio {

struct MidiNote {
    keel::u16 track = 0;
    keel::u8 channel = 0;
    keel::u8 noteNumber = 60; // Middle C
    keel::u8 velocity = 64;
    double startTimeSec = 0.0;
    double endTimeSec = 0.0;
    double durationSec = 0.0;
};

struct MidiInfo {
    keel::u16 format = 0;             // 0 or 1
    keel::u16 numTracks = 0;
    keel::u16 ticksPerQuarterNote = 480;
    double initialTempoBpm = 120.0;
    double duration = 0.0;           // in seconds
    keel::u64 totalNotes = 0;
    keel::String trackName;
};

/// Sniff MIDI 'MThd' magic directly from an in-memory buffer.
bool isMidiBytes(const keel::u8 *data, keel::usize len);
bool isMidiBytes(keel::StringRef b);

/// Peek MIDI file metadata without decoding all note events.
MidiInfo peekMidi(const keel::u8 *data, keel::usize len);

/// Decode MIDI note matrix: [Track, Channel, Note, Velocity, StartTime(s), EndTime(s)] (N x 6 matrix).
Value readMidi(const keel::u8 *data, keel::usize len,
               keel::Allocator *mr = nullptr);

/// Encode note matrix [N x 6] to Standard MIDI File (SMF Format 0).
keel::Array<keel::u8> writeMidiToBytes(const Value &notesMatrix,
                                       keel::u16 ticksPerQuarterNote = 480,
                                       double tempoBpm = 120.0);

} // namespace numkit::audio
