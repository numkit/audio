// toolboxes/audio/src/io/wav_codec.hpp
//
// In-tree RIFF/WAVE reader, writer, and peeker.
// Zero-dependency pure C++17 implementation.

#pragma once

#include <keel/core.h>
#include <keel/memory.h>
#include <numkit/audio/io/audio_io.hpp>

namespace numkit::audio {

/// Sniff RIFF/WAVE header directly from an in-memory buffer.
bool isWavBytes(const keel::u8 *data, keel::usize len);
bool isWavBytes(keel::StringRef b);

/// Decode WAV audio from in-memory byte buffer.
/// @param startSample 1-based start sample index (inclusive). 1 = beginning.
/// @param endSample   1-based end sample index (inclusive). -1 = end of stream.
/// @param nativeType  If true, returns native integer/float ValueType instead of double.
AudioData readWav(const keel::u8 *data, keel::usize len,
                  keel::i64 startSample = 1, keel::i64 endSample = -1,
                  bool nativeType = false,
                  keel::Allocator *mr = nullptr);

/// Peek WAV metadata without decoding sample data.
AudioInfo peekWav(const keel::u8 *data, keel::usize len);

/// Encode audio matrix `y` (N x C) to in-memory WAV byte stream.
/// @param bitsPerSample 8, 16, 24, or 32 (32-bit floats encoded as IEEE Float format 3).
keel::Array<keel::u8> writeWavToBytes(const Value &y, keel::f64 sampleRate,
                                      keel::u16 bitsPerSample = 16,
                                      keel::StringRef title = "",
                                      keel::StringRef artist = "",
                                      keel::StringRef comment = "");

} // namespace numkit::audio
