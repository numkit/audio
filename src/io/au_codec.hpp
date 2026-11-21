// toolboxes/audio/src/io/au_codec.hpp
//
// In-tree Sun/NeXT AU (.au, .snd) reader, writer, and peeker.
// Zero-dependency pure C++17 implementation.

#pragma once

#include <keel/array.h>
#include <keel/core.h>
#include <keel/memory.h>
#include <keel/ref.h>
#include "wav_codec.hpp" // For AudioData and AudioInfo

#include <cstdint>
#include <numkit/value/value.hpp>

namespace numkit::audio {

/// Sniff AU/SND magic bytes directly from an in-memory buffer.
bool isAuBytes(const keel::u8 *data, keel::usize len);
bool isAuBytes(keel::StringRef b);

/// Decode AU/SND audio from in-memory byte buffer.
AudioData readAu(const keel::u8 *data, keel::usize len,
                 keel::i64 startSample = 1, keel::i64 endSample = -1,
                 bool nativeType = false,
                 keel::Allocator *mr = nullptr);

/// Peek AU/SND metadata without decoding full audio stream.
AudioInfo peekAu(const keel::u8 *data, keel::usize len);

/// Encode audio matrix `y` (N x C) to in-memory AU byte stream.
keel::Array<keel::u8> writeAuToBytes(const Value &y, keel::f64 sampleRate,
                                     keel::u16 bitsPerSample = 16,
                                     keel::StringRef comment = "");

} // namespace numkit::audio
