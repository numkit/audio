// modules/audio/src/io/audio_io.cpp
//
// Thin PRIVATE adapter bridging NumKit's Audio & MIDI I/O APIs
// (audioread, audiowrite, audioinfo, midiread, midiwrite, midiinfo)
// to keel::audiofile.

#include <keel/array.h>
#include <keel/math.h>
#include <keel/core.h>
#include <keel/format.h>
#include <keel/fs.h>
#include <keel/memory.h>
#include <keel/ref.h>
#include <keel/string.h>
#include <numkit/audio/io/audio_io.hpp>

#include "wav_codec.hpp"
#include "flac_codec.hpp"
#include "mp3_codec.hpp"
#include "aiff_codec.hpp"
#include "au_codec.hpp"
#include "midi_codec.hpp"

#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>
#include <numkit/value/value.hpp>

#include "keel/audiofile.h"
#include "keel/midi.h"

#include <keel/utf8.h>

namespace numkit::audio {

namespace {

keel::ArrayRef<keel::u8> toRef(const keel::u8 *data, keel::usize len) {
    if (!data || len == 0) return {};
    return keel::ArrayRef<keel::u8>(data, len);
}

keel::String extensionFromPath(keel::StringRef path) {
    auto dot = path.rfind('.');
    if (dot == keel::kInvalidIndex) return keel::String{};
    keel::String ext = keel::String::from(path.substr(dot + 1));
    ext.to_ascii_lower_in_place();
    return ext;
}

keel::Array<keel::u8> readFileDirect(keel::StringRef path) {
    auto res = keel::fs_read_file(path);
    if (!res) {
        throw Error(keel::format("audioread: could not open file: {}", path),
                    0, 0, "audioread", "", "numkit:audioread:fileNotFound");
    }
    return keel::move(res.value());
}

void writeFileDirect(keel::StringRef path, keel::ArrayRef<keel::u8> data) {
    auto err = keel::fs_write_file(path, data);
    if (err.is_err()) {
        throw Error(keel::format("audiowrite: could not create output file: {}", path),
                    0, 0, "audiowrite", "", "numkit:audiowrite:fileCreateFailed");
    }
}

void audioread_argcheck(keel::audiofile::AudioFormat fmt) {
    if (fmt == keel::audiofile::AudioFormat::Unknown || fmt == keel::audiofile::AudioFormat::Midi) {
        throw Error("audioread: unrecognized or unsupported audio file format",
                    0, 0, "audioread", "", "numkit:audioread:unsupportedFormat");
    }
}

void audioinfo_argcheck(keel::audiofile::AudioFormat fmt) {
    if (fmt == keel::audiofile::AudioFormat::Unknown || fmt == keel::audiofile::AudioFormat::Midi) {
        throw Error("audioinfo: unrecognized or unsupported audio file format",
                    0, 0, "audioinfo", "", "numkit:audioinfo:unsupportedFormat");
    }
}

void audiowrite_argcheck(keel::StringRef fmt, keel::StringRef format) {
    if (fmt != "wav" && fmt != "wave" &&
        fmt != "aiff" && fmt != "aif" && fmt != "aifc" &&
        fmt != "au" && fmt != "snd") {
        throw Error(keel::format("audiowrite: unsupported format for audio encoding: '{}'", format),
                    0, 0, "audiowrite", "", "numkit:audiowrite:unsupportedFormat");
    }
}

void audiowriteMatrix_argcheck(keel::usize numFrames, keel::usize numChannels) {
    if (numFrames == 0 || numChannels == 0) {
        throw Error("audiowrite: audio matrix cannot be empty",
                    0, 0, "audiowrite", "", "numkit:audiowrite:empty");
    }
}

keel::audiofile::AudioDecodeOptions makeDecodeOpts(keel::i64 startSample, keel::i64 endSample, bool nativeType) {
    keel::audiofile::AudioDecodeOptions opts;
    const keel::i64 s0 = (startSample < 1) ? 0 : (startSample - 1);
    opts.start_frame = keel::as<keel::u64>(s0);
    if (endSample < 0) {
        opts.end_frame = keel::Limits<keel::u64>::kMax;
    } else {
        const keel::u64 s1 = keel::as<keel::u64>(endSample);
        opts.end_frame = (s1 < opts.start_frame) ? opts.start_frame : s1;
    }
    opts.native_sample_type = nativeType;
    opts.layout = keel::audiofile::AudioChannelLayout::Planar;
    return opts;
}

AudioData bufferToAudioData(const keel::audiofile::AudioBuffer &buf, bool nativeType,
                            keel::Allocator *mr) {
    AudioData res;
    res.sampleRate = buf.sample_rate;
    res.bitsPerSample = buf.bits_per_sample;
    res.numChannels = buf.channels;
    res.totalSamples = buf.total_source_frames;
    res.title = keel::String::from(buf.metadata.title.as_ref());
    res.artist = keel::String::from(buf.metadata.artist.as_ref());
    res.comment = keel::String::from(buf.metadata.comment.as_ref());

    const keel::usize frames = keel::as<keel::usize>(buf.frames);
    const keel::usize channels = keel::as<keel::usize>(buf.channels);
    if (frames == 0) {
        res.y = Value::matrix(0, channels, ValueType::DOUBLE, mr);
        return res;
    }

    if (!nativeType) {
        res.y = Value::matrix(frames, channels, ValueType::DOUBLE, mr);
        keel::mem_copy(res.y.doubleDataMut(), buf.data.data(), frames * channels * sizeof(keel::f64));
        return res;
    }

    switch (buf.sample_type) {
        case keel::audiofile::AudioSampleType::U8:
            res.y = Value::matrix(frames, channels, ValueType::UINT8, mr);
            keel::mem_copy(res.y.uint8DataMut(), buf.data.data(), frames * channels);
            break;
        case keel::audiofile::AudioSampleType::I8:
            res.y = Value::matrix(frames, channels, ValueType::INT16, mr);
            for (keel::usize i = 0; i < frames * channels; ++i) {
                const auto s8 = keel::as<keel::i8>(buf.data[i]);
                res.y.int16DataMut()[i] = keel::as<keel::i16>(keel::as<keel::i32>(s8) << 8);
            }
            break;
        case keel::audiofile::AudioSampleType::I16:
            res.y = Value::matrix(frames, channels, ValueType::INT16, mr);
            keel::mem_copy(res.y.int16DataMut(), buf.data.data(), frames * channels * sizeof(keel::i16));
            break;
        case keel::audiofile::AudioSampleType::I32:
            res.y = Value::matrix(frames, channels, ValueType::INT32, mr);
            keel::mem_copy(res.y.int32DataMut(), buf.data.data(), frames * channels * sizeof(keel::i32));
            break;
        case keel::audiofile::AudioSampleType::F32:
            res.y = Value::matrix(frames, channels, ValueType::SINGLE, mr);
            keel::mem_copy(res.y.singleDataMut(), buf.data.data(), frames * channels * sizeof(keel::f32));
            break;
        case keel::audiofile::AudioSampleType::F64:
            res.y = Value::matrix(frames, channels, ValueType::DOUBLE, mr);
            keel::mem_copy(res.y.doubleDataMut(), buf.data.data(), frames * channels * sizeof(keel::f64));
            break;
    }
    return res;
}

AudioInfo keelInfoToAudioInfo(const keel::audiofile::AudioInfo &ki) {
    AudioInfo info;
    info.format = keel::String::from(keel::audiofile::audio_format_name(ki.format));
    info.compressionMethod = keel::String::from(ki.compression_method.as_ref());
    info.numChannels = ki.channels;
    info.sampleRate = ki.sample_rate;
    info.totalSamples = ki.total_frames;
    info.duration = ki.duration_sec;
    info.bitsPerSample = ki.bits_per_sample;
    info.bitRate = ki.bit_rate_kbps;
    info.title = keel::String::from(ki.metadata.title.as_ref());
    info.artist = keel::String::from(ki.metadata.artist.as_ref());
    info.comment = keel::String::from(ki.metadata.comment.as_ref());
    return info;
}

struct PreparedAudioInput {
    keel::Array<keel::f64> promoted;
    keel::audiofile::AudioView view;
};

PreparedAudioInput prepareEncodeInput(const Value &y, keel::f64 sampleRate, bool preserveSingle) {
    const auto &d = y.dims();
    const keel::usize numFrames = d.rows();
    const keel::usize numChannels = (d.ndim() >= 2) ? d.cols() : 1;
    audiowriteMatrix_argcheck(numFrames, numChannels);

    if (sampleRate <= 0.0) sampleRate = 44100.0;

    PreparedAudioInput out;
    if (preserveSingle && y.type() == ValueType::SINGLE) {
        out.view = keel::audiofile::AudioView::from_f32(
            keel::ArrayRef<keel::f32>(y.singleData(), numFrames * numChannels),
            numFrames, keel::as<keel::u16>(numChannels), sampleRate,
            keel::audiofile::AudioChannelLayout::Planar);
        return out;
    }
    if (y.type() == ValueType::DOUBLE) {
        out.view = keel::audiofile::AudioView::from_f64(
            keel::ArrayRef<keel::f64>(y.doubleData(), numFrames * numChannels),
            numFrames, keel::as<keel::u16>(numChannels), sampleRate,
            keel::audiofile::AudioChannelLayout::Planar);
        return out;
    }

    out.promoted.resize(numFrames * numChannels);
    for (keel::usize i = 0; i < numFrames * numChannels; ++i) {
        out.promoted[i] = y.elemAsDouble(i);
    }
    out.view = keel::audiofile::AudioView::from_f64(
        keel::ArrayRef<keel::f64>(out.promoted.data(), out.promoted.size()),
        numFrames, keel::as<keel::u16>(numChannels), sampleRate,
        keel::audiofile::AudioChannelLayout::Planar);
    return out;
}

} // anonymous namespace

// ============================================================================
// Format Sniffers
// ============================================================================

bool isWavBytes(const keel::u8 *data, keel::usize len) {
    return keel::audiofile::is_wav_bytes(toRef(data, len));
}
bool isWavBytes(keel::StringRef b) {
    return isWavBytes(reinterpret_cast<const keel::u8 *>(b.data()), b.size());
}

bool isFlacBytes(const keel::u8 *data, keel::usize len) {
    return keel::audiofile::is_flac_bytes(toRef(data, len));
}
bool isFlacBytes(keel::StringRef b) {
    return isFlacBytes(reinterpret_cast<const keel::u8 *>(b.data()), b.size());
}

bool isMp3Bytes(const keel::u8 *data, keel::usize len) {
    return keel::audiofile::is_mp3_bytes(toRef(data, len));
}
bool isMp3Bytes(keel::StringRef b) {
    return isMp3Bytes(reinterpret_cast<const keel::u8 *>(b.data()), b.size());
}

bool isAiffBytes(const keel::u8 *data, keel::usize len) {
    return keel::audiofile::is_aiff_bytes(toRef(data, len));
}
bool isAiffBytes(keel::StringRef b) {
    return isAiffBytes(reinterpret_cast<const keel::u8 *>(b.data()), b.size());
}

bool isAuBytes(const keel::u8 *data, keel::usize len) {
    return keel::audiofile::is_au_bytes(toRef(data, len));
}
bool isAuBytes(keel::StringRef b) {
    return isAuBytes(reinterpret_cast<const keel::u8 *>(b.data()), b.size());
}

bool isMidiBytes(const keel::u8 *data, keel::usize len) {
    return keel::audiofile::is_midi_bytes(toRef(data, len));
}
bool isMidiBytes(keel::StringRef b) {
    return isMidiBytes(reinterpret_cast<const keel::u8 *>(b.data()), b.size());
}

// ============================================================================
// Per-Codec Readers, Peekers, and Writers
// ============================================================================

AudioData readWav(const keel::u8 *data, keel::usize len,
                  keel::i64 startSample, keel::i64 endSample,
                  bool nativeType, keel::Allocator *mr) {
    auto res = keel::audiofile::decode_wav(toRef(data, len), makeDecodeOpts(startSample, endSample, nativeType));
    if (!res) {
        throw Error("audioread: failed to decode WAV stream",
                    0, 0, "audioread", "", "numkit:audioread:wav");
    }
    return bufferToAudioData(res.value(), nativeType, mr);
}

AudioInfo peekWav(const keel::u8 *data, keel::usize len) {
    auto res = keel::audiofile::peek_wav(toRef(data, len));
    if (!res) {
        throw Error("audioinfo: not a valid RIFF/WAVE audio stream",
                    0, 0, "audioinfo", "", "numkit:audioinfo:wav");
    }
    return keelInfoToAudioInfo(res.value());
}

keel::Array<keel::u8> writeWavToBytes(const Value &y, keel::f64 sampleRate,
                                      keel::u16 bitsPerSample,
                                      keel::StringRef title,
                                      keel::StringRef artist,
                                      keel::StringRef comment) {
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 &&
        bitsPerSample != 32 && bitsPerSample != 64) {
        bitsPerSample = 16;
    }
    auto input = prepareEncodeInput(y, sampleRate, /*preserveSingle=*/(bitsPerSample == 32));
    keel::audiofile::AudioEncodeOptions opts;
    opts.bits_per_sample = bitsPerSample;
    opts.float_samples = (y.type() == ValueType::SINGLE && bitsPerSample == 32) || (bitsPerSample == 64);
    opts.title = title;
    opts.artist = artist;
    opts.comment = comment;

    auto res = keel::audiofile::encode_wav(input.view, opts);
    if (!res) {
        throw Error("audiowrite: failed to encode WAV stream",
                    0, 0, "audiowrite", "", "numkit:audiowrite:wav");
    }
    return keel::move(res.value());
}

AudioData readFlac(const keel::u8 *data, keel::usize len,
                   keel::i64 startSample, keel::i64 endSample,
                   bool nativeType, keel::Allocator *mr) {
    auto res = keel::audiofile::decode_flac(toRef(data, len), makeDecodeOpts(startSample, endSample, nativeType));
    if (!res) {
        throw Error("audioread: failed to decode FLAC stream",
                    0, 0, "audioread", "", "numkit:audioread:flac");
    }
    return bufferToAudioData(res.value(), nativeType, mr);
}

AudioInfo peekFlac(const keel::u8 *data, keel::usize len) {
    auto res = keel::audiofile::peek_flac(toRef(data, len));
    if (!res) {
        throw Error("audioinfo: not a valid FLAC audio stream",
                    0, 0, "audioinfo", "", "numkit:audioinfo:flac");
    }
    return keelInfoToAudioInfo(res.value());
}

AudioData readMp3(const keel::u8 *data, keel::usize len,
                  keel::i64 startSample, keel::i64 endSample,
                  bool nativeType, keel::Allocator *mr) {
    auto res = keel::audiofile::decode_mp3(toRef(data, len), makeDecodeOpts(startSample, endSample, nativeType));
    if (!res) {
        throw Error("audioread: failed to decode MP3 stream",
                    0, 0, "audioread", "", "numkit:audioread:mp3");
    }
    return bufferToAudioData(res.value(), nativeType, mr);
}

AudioInfo peekMp3(const keel::u8 *data, keel::usize len) {
    auto res = keel::audiofile::peek_mp3(toRef(data, len));
    if (!res) {
        throw Error("audioinfo: could not find valid MPEG Audio Layer III frame sync",
                    0, 0, "audioinfo", "", "numkit:audioinfo:mp3");
    }
    return keelInfoToAudioInfo(res.value());
}

AudioData readAiff(const keel::u8 *data, keel::usize len,
                   keel::i64 startSample, keel::i64 endSample,
                   bool nativeType, keel::Allocator *mr) {
    auto res = keel::audiofile::decode_aiff(toRef(data, len), makeDecodeOpts(startSample, endSample, nativeType));
    if (!res) {
        throw Error("audioread: failed to decode AIFF stream",
                    0, 0, "audioread", "", "numkit:audioread:aiff");
    }
    return bufferToAudioData(res.value(), nativeType, mr);
}

AudioInfo peekAiff(const keel::u8 *data, keel::usize len) {
    auto res = keel::audiofile::peek_aiff(toRef(data, len));
    if (!res) {
        throw Error("audioinfo: not a valid AIFF audio stream",
                    0, 0, "audioinfo", "", "numkit:audioinfo:aiff");
    }
    return keelInfoToAudioInfo(res.value());
}

keel::Array<keel::u8> writeAiffToBytes(const Value &y, keel::f64 sampleRate,
                                       keel::u16 bitsPerSample,
                                       keel::StringRef title,
                                       keel::StringRef artist,
                                       keel::StringRef comment) {
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
        bitsPerSample = 16;
    }
    auto input = prepareEncodeInput(y, sampleRate, /*preserveSingle=*/false);
    keel::audiofile::AudioEncodeOptions opts;
    opts.bits_per_sample = bitsPerSample;
    opts.float_samples = false;
    opts.title = title;
    opts.artist = artist;
    opts.comment = comment;

    auto res = keel::audiofile::encode_aiff(input.view, opts);
    if (!res) {
        throw Error("audiowrite: failed to encode AIFF stream",
                    0, 0, "audiowrite", "", "numkit:audiowrite:aiff");
    }
    return keel::move(res.value());
}

AudioData readAu(const keel::u8 *data, keel::usize len,
                 keel::i64 startSample, keel::i64 endSample,
                 bool nativeType, keel::Allocator *mr) {
    auto res = keel::audiofile::decode_au(toRef(data, len), makeDecodeOpts(startSample, endSample, nativeType));
    if (!res) {
        throw Error("audioread: failed to decode AU/SND stream",
                    0, 0, "audioread", "", "numkit:audioread:au");
    }
    return bufferToAudioData(res.value(), nativeType, mr);
}

AudioInfo peekAu(const keel::u8 *data, keel::usize len) {
    auto res = keel::audiofile::peek_au(toRef(data, len));
    if (!res) {
        throw Error("audioinfo: not a valid AU/SND audio stream",
                    0, 0, "audioinfo", "", "numkit:audioinfo:au");
    }
    return keelInfoToAudioInfo(res.value());
}

keel::Array<keel::u8> writeAuToBytes(const Value &y, keel::f64 sampleRate,
                                     keel::u16 bitsPerSample,
                                     keel::StringRef comment) {
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
        bitsPerSample = 16;
    }
    auto input = prepareEncodeInput(y, sampleRate, /*preserveSingle=*/false);
    keel::audiofile::AudioEncodeOptions opts;
    opts.bits_per_sample = bitsPerSample;
    opts.float_samples = false;
    opts.comment = comment;

    auto res = keel::audiofile::encode_au(input.view, opts);
    if (!res) {
        throw Error("audiowrite: failed to encode AU/SND stream",
                    0, 0, "audiowrite", "", "numkit:audiowrite:au");
    }
    return keel::move(res.value());
}

// ============================================================================
// Standard MIDI File (SMF 0/1) Adapter
// ============================================================================

MidiInfo peekMidi(const keel::u8 *data, keel::usize len) {
    auto res = keel::audiofile::peek_midi(toRef(data, len));
    if (!res) {
        throw Error("midiinfo: not a valid MIDI file",
                    0, 0, "midiinfo", "", "numkit:midiinfo:badMagic");
    }
    const auto &km = res.value();
    MidiInfo info;
    info.format = km.format;
    info.numTracks = km.num_tracks;
    info.ticksPerQuarterNote = km.ticks_per_quarter_note;
    info.initialTempoBpm = km.initial_tempo_bpm;
    info.duration = km.duration_sec;
    info.totalNotes = km.total_notes;
    info.trackName = keel::String::from(km.track_name.as_ref());
    return info;
}

Value readMidi(const keel::u8 *data, keel::usize len,
               keel::Allocator *mr) {
    auto res = keel::audiofile::decode_midi(toRef(data, len));
    if (!res) {
        throw Error("midiread: not a valid MIDI file",
                    0, 0, "midiread", "", "numkit:midiread:badMagic");
    }
    const auto &notes = res.value();
    const keel::usize numNotes = notes.size();
    Value out = Value::matrix(numNotes, 6, ValueType::DOUBLE, mr);
    keel::f64 *dst = out.doubleDataMut();
    for (keel::usize i = 0; i < numNotes; ++i) {
        const auto &n = notes[i];
        dst[0 * numNotes + i] = keel::as<keel::f64>(n.track + 1u);
        dst[1 * numNotes + i] = keel::as<keel::f64>(n.channel + 1u);
        dst[2 * numNotes + i] = keel::as<keel::f64>(n.note_number);
        dst[3 * numNotes + i] = keel::as<keel::f64>(n.velocity);
        dst[4 * numNotes + i] = n.start_time_sec;
        dst[5 * numNotes + i] = n.end_time_sec;
    }
    return out;
}

keel::Array<keel::u8> writeMidiToBytes(const Value &notesMatrix,
                                       keel::u16 ticksPerQuarterNote,
                                       keel::f64 tempoBpm) {
    const auto &d = notesMatrix.dims();
    const keel::usize numNotes = d.rows();
    if (ticksPerQuarterNote == 0 || (ticksPerQuarterNote & 0x8000u) != 0u) {
        ticksPerQuarterNote = 480;
    }
    if (!keel::is_finite(tempoBpm) || tempoBpm <= 0.0) {
        tempoBpm = 120.0;
    }

    keel::Array<keel::audiofile::MidiNote> notes(numNotes);
    for (keel::usize i = 0; i < numNotes; ++i) {
        keel::i32 trk = 0;
        if (d.cols() >= 1) {
            trk = keel::max(0, keel::as<keel::i32>(notesMatrix.elemAsDouble(0 * numNotes + i)) - 1);
        }
        keel::i32 ch = 0;
        if (d.cols() >= 2) {
            ch = keel::clamp(keel::as<keel::i32>(notesMatrix.elemAsDouble(1 * numNotes + i)) - 1, 0, 15);
        }
        keel::i32 noteNum = 60;
        if (d.cols() >= 3) {
            noteNum = keel::clamp(keel::as<keel::i32>(notesMatrix.elemAsDouble(2 * numNotes + i)), 0, 127);
        }
        keel::i32 vel = 64;
        if (d.cols() >= 4) {
            vel = keel::clamp(keel::as<keel::i32>(notesMatrix.elemAsDouble(3 * numNotes + i)), 0, 127);
        }
        const keel::f64 startSec = (d.cols() >= 5) ? keel::max(0.0, notesMatrix.elemAsDouble(4 * numNotes + i)) : 0.0;
        const keel::f64 endSec = (d.cols() >= 6)
            ? keel::max(startSec, notesMatrix.elemAsDouble(5 * numNotes + i))
            : (startSec + 0.5);

        keel::audiofile::MidiNote kn;
        kn.track = keel::as<keel::u16>(trk);
        kn.channel = keel::as<keel::u8>(ch);
        kn.note_number = keel::as<keel::u8>(noteNum);
        kn.velocity = keel::as<keel::u8>(vel);
        kn.start_time_sec = startSec;
        kn.end_time_sec = endSec;
        kn.duration_sec = endSec - startSec;
        notes[i] = kn;
    }

    keel::audiofile::MidiEncodeOptions opts;
    opts.ticks_per_quarter_note = ticksPerQuarterNote;
    opts.tempo_bpm = tempoBpm;

    auto res = keel::audiofile::encode_midi(notes.as_ref(), opts);
    if (!res) {
        throw Error("midiwrite: failed to encode MIDI notes",
                    0, 0, "midiwrite", "", "numkit:midiwrite:encode");
    }
    return keel::move(res.value());
}

// ============================================================================
// Public Unified Audio I/O Dispatcher
// ============================================================================

AudioData audioreadFromBytes(const keel::u8 *data, keel::usize len,
                             keel::i64 startSample, keel::i64 endSample,
                             bool nativeType,
                             keel::Allocator *mr) {
    const keel::audiofile::AudioFormat fmt = keel::audiofile::sniff_audio_format(toRef(data, len));
    audioread_argcheck(fmt);

    switch (fmt) {
        case keel::audiofile::AudioFormat::Wav:  return readWav(data, len, startSample, endSample, nativeType, mr);
        case keel::audiofile::AudioFormat::Flac: return readFlac(data, len, startSample, endSample, nativeType, mr);
        case keel::audiofile::AudioFormat::Mp3:  return readMp3(data, len, startSample, endSample, nativeType, mr);
        case keel::audiofile::AudioFormat::Aiff: return readAiff(data, len, startSample, endSample, nativeType, mr);
        case keel::audiofile::AudioFormat::Au:   return readAu(data, len, startSample, endSample, nativeType, mr);
        default:
            break;
    }
    throw Error("audioread: unrecognized or unsupported audio file format",
                0, 0, "audioread", "", "numkit:audioread:unsupportedFormat");
}

AudioData audioreadFromBytes(keel::StringRef bytes,
                             keel::i64 startSample, keel::i64 endSample,
                             bool nativeType,
                             keel::Allocator *mr) {
    return audioreadFromBytes(reinterpret_cast<const keel::u8 *>(bytes.data()),
                              bytes.size(), startSample, endSample, nativeType, mr);
}

AudioInfo audioinfoFromBytes(const keel::u8 *data, keel::usize len) {
    const keel::audiofile::AudioFormat fmt = keel::audiofile::sniff_audio_format(toRef(data, len));
    audioinfo_argcheck(fmt);

    switch (fmt) {
        case keel::audiofile::AudioFormat::Wav:  return peekWav(data, len);
        case keel::audiofile::AudioFormat::Flac: return peekFlac(data, len);
        case keel::audiofile::AudioFormat::Mp3:  return peekMp3(data, len);
        case keel::audiofile::AudioFormat::Aiff: return peekAiff(data, len);
        case keel::audiofile::AudioFormat::Au:   return peekAu(data, len);
        default:
            break;
    }
    throw Error("audioinfo: unrecognized or unsupported audio file format",
                0, 0, "audioinfo", "", "numkit:audioinfo:unsupportedFormat");
}

AudioInfo audioinfoFromBytes(keel::StringRef bytes) {
    return audioinfoFromBytes(reinterpret_cast<const keel::u8 *>(bytes.data()), bytes.size());
}

keel::Array<keel::u8> audiowriteToBytes(const Value &y, keel::f64 sampleRate,
                                        keel::StringRef format,
                                        keel::u16 bitsPerSample,
                                        keel::StringRef title,
                                        keel::StringRef artist,
                                        keel::StringRef comment) {
    keel::String fmt = keel::String::from(format);
    fmt.to_ascii_lower_in_place();
    audiowrite_argcheck(fmt.as_ref(), format);

    if (fmt == "wav" || fmt == "wave") {
        return writeWavToBytes(y, sampleRate, bitsPerSample, title, artist, comment);
    } else if (fmt == "aiff" || fmt == "aif" || fmt == "aifc") {
        return writeAiffToBytes(y, sampleRate, bitsPerSample, title, artist, comment);
    } else if (fmt == "au" || fmt == "snd") {
        return writeAuToBytes(y, sampleRate, bitsPerSample, comment);
    }

    throw Error(keel::format("audiowrite: unsupported format for audio encoding: '{}'", format),
                0, 0, "audiowrite", "", "numkit:audiowrite:unsupportedFormat");
}

AudioData audioread(keel::StringRef path,
                    keel::i64 startSample, keel::i64 endSample,
                    bool nativeType,
                    keel::Allocator *mr) {
    auto bytes = readFileDirect(path);
    return audioreadFromBytes(bytes.data(), bytes.size(), startSample, endSample, nativeType, mr);
}

AudioInfo audioinfo(keel::StringRef path) {
    auto bytes = readFileDirect(path);
    auto info = audioinfoFromBytes(bytes.data(), bytes.size());
    info.title = keel::String::from(path);
    return info;
}

void audiowrite(keel::StringRef path, const Value &y, keel::f64 sampleRate,
                keel::u16 bitsPerSample,
                keel::StringRef title,
                keel::StringRef artist,
                keel::StringRef comment) {
    keel::String ext = extensionFromPath(path);
    if (ext.empty()) ext.assign("wav");
    auto bytes = audiowriteToBytes(y, sampleRate, ext.as_ref(), bitsPerSample, title, artist, comment);
    writeFileDirect(path, bytes.as_ref());
}

} // namespace numkit::audio
