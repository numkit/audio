// toolboxes/audio/src/io/mp3_codec.hpp
//
// In-tree MP3 (MPEG-1/2/2.5 Audio Layer III) reader and peeker.
// Zero-dependency pure C++17 implementation.

#pragma once

#include <keel/core.h>
#include <keel/memory.h>
#include <keel/ref.h>
#include "wav_codec.hpp" // For AudioData and AudioInfo

#include <numkit/value/value.hpp>

namespace numkit::audio {

/// Sniff MP3 magic or frame sync directly from an in-memory buffer.
bool isMp3Bytes(const keel::u8 *data, keel::usize len);
bool isMp3Bytes(keel::StringRef b);

/// Decode MP3 audio from in-memory byte buffer.
AudioData readMp3(const keel::u8 *data, keel::usize len,
                  keel::i64 startSample = 1, keel::i64 endSample = -1,
                  bool nativeType = false,
                  keel::Allocator *mr = nullptr);

/// Peek MP3 metadata and ID3 tags without decoding full stream.
AudioInfo peekMp3(const keel::u8 *data, keel::usize len);

} // namespace numkit::audio
