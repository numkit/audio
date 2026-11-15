// toolboxes/audio/src/io/flac_codec.hpp
//
// In-tree FLAC (Free Lossless Audio Codec) reader and peeker.
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

/// Sniff FLAC magic bytes directly from an in-memory buffer.
bool isFlacBytes(const keel::u8 *data, keel::usize len);
bool isFlacBytes(const std::string &b);

/// Decode FLAC audio from in-memory byte buffer.
AudioData readFlac(const keel::u8 *data, keel::usize len,
                   keel::i64 startSample = 1, keel::i64 endSample = -1,
                   bool nativeType = false,
                   keel::Allocator *mr = nullptr);

/// Peek FLAC metadata without decoding all audio frames.
AudioInfo peekFlac(const keel::u8 *data, keel::usize len);

} // namespace numkit::audio
