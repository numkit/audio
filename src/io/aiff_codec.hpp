// toolboxes/audio/src/io/aiff_codec.hpp
//
// In-tree AIFF / AIFF-C (Apple / Big-Endian PCM) reader, writer, and peeker.
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

/// Sniff AIFF magic bytes directly from an in-memory buffer.
bool isAiffBytes(const keel::u8 *data, keel::usize len);
bool isAiffBytes(const std::string &b);

/// Decode AIFF audio from in-memory byte buffer.
AudioData readAiff(const keel::u8 *data, keel::usize len,
                   keel::i64 startSample = 1, keel::i64 endSample = -1,
                   bool nativeType = false,
                   keel::Allocator *mr = nullptr);

/// Peek AIFF metadata without decoding full audio stream.
AudioInfo peekAiff(const keel::u8 *data, keel::usize len);

/// Encode audio matrix `y` (N x C) to in-memory AIFF byte stream.
std::vector<keel::u8> writeAiffToBytes(const Value &y, double sampleRate,
                                           keel::u16 bitsPerSample = 16,
                                           const std::string &title = "",
                                           const std::string &artist = "",
                                           const std::string &comment = "");

} // namespace numkit::audio
