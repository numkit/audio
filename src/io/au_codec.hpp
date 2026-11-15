// toolboxes/audio/src/io/au_codec.hpp
//
// In-tree Sun/NeXT AU (.au, .snd) reader, writer, and peeker.
// Zero-dependency pure C++17 implementation.

#pragma once

#include <keel/core.h>
#include <keel/memory.h>
#include "wav_codec.hpp" // For AudioData and AudioInfo

#include <cstdint>
#include <numkit/value/value.hpp>
#include <string>
#include <vector>

namespace numkit::audio {

/// Sniff AU/SND magic bytes directly from an in-memory buffer.
bool isAuBytes(const keel::u8 *data, keel::usize len);
bool isAuBytes(const std::string &b);

/// Decode AU/SND audio from in-memory byte buffer.
AudioData readAu(const keel::u8 *data, keel::usize len,
                 keel::i64 startSample = 1, keel::i64 endSample = -1,
                 bool nativeType = false,
                 keel::Allocator *mr = nullptr);

/// Peek AU/SND metadata without decoding full audio stream.
AudioInfo peekAu(const keel::u8 *data, keel::usize len);

/// Encode audio matrix `y` (N x C) to in-memory AU byte stream.
std::vector<keel::u8> writeAuToBytes(const Value &y, double sampleRate,
                                         keel::u16 bitsPerSample = 16,
                                         const std::string &comment = "");

} // namespace numkit::audio
