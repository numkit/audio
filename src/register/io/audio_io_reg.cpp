// bundle/src/register/audio/io/audio_io_reg.cpp
//
// Registration of audioread, audiowrite, audioinfo, midiread, midiwrite, midiinfo
// into the NK engine with Virtual Filesystem (VFS) integration.

#include <keel/core.h>
#include <keel/format.h>
#include <keel/ref.h>
#include <keel/string.h>
#include <keel/utf8.h>
#include <numkit/audio/io/audio_io.hpp>
#include "io/midi_codec.hpp"

#include <numkit/sys/plugin.hpp>
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>
#include <numkit/value/value.hpp>


namespace numkit::audio::detail {

namespace {

keel::String lowerExt(keel::StringRef path) {
    auto dot = path.rfind('.');
    if (dot == keel::kInvalidIndex) return keel::String{};
    keel::String ext = keel::String::from(path.substr(dot + 1));
    ext.to_ascii_lower_in_place();
    return ext;
}

} // anonymous

void audioread_reg(keel::ArrayRef<Value> args, keel::usize nargout, keel::MutArrayRef<Value> outs,
                   CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 1, 3, "audioread");
    if (!args[0].isChar() && !args[0].isString()) {
        throw Error("audioread: filename must be a string",
                    0, 0, "audioread", "", "numkit:audioread:type");
    }

    const keel::String path = args[0].toString();
    keel::i64 startSample = 1;
    keel::i64 endSample = -1;
    bool nativeType = false;

    for (keel::usize i = 1; i < args.size(); ++i) {
        if (args[i].isChar() || args[i].isString()) {
            keel::String opt = args[i].toString();
            opt.to_ascii_lower_in_place();
            if (opt == "native") nativeType = true;
            else if (opt == "double") nativeType = false;
        } else if (args[i].isNumeric()) {
            if (args[i].isComplex()) {
                throw Error("audioread: samples range must be real numeric",
                            0, 0, "audioread", "", "numkit:audioread:samples");
            }
            const keel::usize n = args[i].numel();
            if (n >= 1) {
                const keel::f64 s0 = args[i].elemAsDouble(0);
                if (!keel::is_finite(s0) || !keel::is_integer(s0)
                    || s0 < 1.0 || s0 > 9007199254740992.0) {
                    throw Error("audioread: start sample must be a finite positive integer",
                                0, 0, "audioread", "", "numkit:audioread:samples");
                }
                startSample = keel::as<keel::i64>(s0);
            }
            if (n >= 2) {
                const keel::f64 s1 = args[i].elemAsDouble(1);
                if (keel::is_inf(s1) && s1 > 0.0) {
                    endSample = -1;
                } else {
                    if (!keel::is_finite(s1) || !keel::is_integer(s1)
                        || s1 < 1.0 || s1 > 9007199254740992.0) {
                        throw Error("audioread: end sample must be a positive integer or Inf",
                                    0, 0, "audioread", "", "numkit:audioread:samples");
                    }
                    endSample = keel::as<keel::i64>(s1);
                }
            }
        }
    }

    // Resolve path via VFS
    auto rp = ctx.engine->resolvePath(path.as_ref());
    if (!rp.fs || !rp.fs->exists(rp.path)) {
        throw Error(keel::format("audioread: failed to read '{}' — file not found", path),
                    0, 0, "audioread", "", "numkit:audioread:fileNotFound");
    }

    const auto bytes = rp.fs->readFileBytes(rp.path);
    AudioData data = audioreadFromBytes(reinterpret_cast<const keel::u8 *>(bytes.data()),
                                       bytes.size(), startSample, endSample, nativeType,
                                       ctx.engine->allocator());

    outs[0] = keel::move(data.y);
    if (nargout >= 2 && outs.size() >= 2) {
        outs[1] = Value::scalar(data.sampleRate, ctx.engine->allocator());
    }
}

void audiowrite_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/, keel::MutArrayRef<Value> /*outs*/,
                    CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 3, "audiowrite");
    if (!args[0].isChar() && !args[0].isString()) {
        throw Error("audiowrite: filename must be a string",
                    0, 0, "audiowrite", "", "numkit:audiowrite:type");
    }
    if (!args[1].isNumeric()) {
        throw Error("audiowrite: audio data must be a numeric matrix",
                    0, 0, "audiowrite", "", "numkit:audiowrite:data");
    }

    const keel::String path = args[0].toString();
    const Value &y = args[1];
    if (!args[2].isNumeric() || args[2].isComplex() || args[2].numel() != 1) {
        throw Error("audiowrite: sample rate Fs must be a positive real numeric scalar",
                    0, 0, "audiowrite", "", "numkit:audiowrite:sampleRate");
    }
    const keel::f64 sampleRate = args[2].elemAsDouble(0);
    if (!keel::is_finite(sampleRate) || !(sampleRate > 0.0)) {
        throw Error("audiowrite: sample rate Fs must be finite and positive",
                    0, 0, "audiowrite", "", "numkit:audiowrite:sampleRate");
    }

    keel::String ext = lowerExt(path.as_ref());
    if (ext.empty()) ext.assign("wav");

    keel::u16 bitsPerSample = 16;
    keel::String title;
    keel::String artist;
    keel::String comment;

    // Parse NV pairs
    for (keel::usize i = 3; i + 1 < args.size(); i += 2) {
        if (!args[i].isChar() && !args[i].isString()) continue;
        keel::String key = args[i].toString();
        key.to_ascii_lower_in_place();
        if (key == "bitspersample") {
            const Value &bpsVal = args[i + 1];
            if (!bpsVal.isNumeric() || bpsVal.isComplex() || bpsVal.numel() != 1) {
                throw Error("audiowrite: BitsPerSample must be a positive real numeric scalar",
                            0, 0, "audiowrite", "", "numkit:audiowrite:bitsPerSample");
            }
            const keel::f64 bps = bpsVal.elemAsDouble(0);
            if (!keel::is_finite(bps) || !keel::is_integer(bps) || bps < 1.0 || bps > 65535.0) {
                throw Error("audiowrite: BitsPerSample must be an integer in [1, 65535]",
                            0, 0, "audiowrite", "", "numkit:audiowrite:bitsPerSample");
            }
            bitsPerSample = keel::as<keel::u16>(bps);
        } else if (key == "title") {
            title = args[i+1].toString();
        } else if (key == "artist") {
            artist = args[i+1].toString();
        } else if (key == "comment") {
            comment = args[i+1].toString();
        }
    }

    auto outBytes = audiowriteToBytes(y, sampleRate, ext.as_ref(), bitsPerSample,
                                      title.as_ref(), artist.as_ref(), comment.as_ref());

    auto rp = ctx.engine->resolvePath(path.as_ref());
    if (!rp.fs) {
        throw Error(keel::format("audiowrite: no filesystem for '{}'", path),
                    0, 0, "audiowrite", "", "numkit:audiowrite:fs");
    }

    rp.fs->writeFileBytes(rp.path, keel::StringRef(reinterpret_cast<const char *>(outBytes.data()), outBytes.size()));
}

void audioinfo_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/, keel::MutArrayRef<Value> outs,
                   CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 1, 1, "audioinfo");
    if (!args[0].isChar() && !args[0].isString()) {
        throw Error("audioinfo: filename must be a string",
                    0, 0, "audioinfo", "", "numkit:audioinfo:type");
    }

    const keel::String path = args[0].toString();
    auto rp = ctx.engine->resolvePath(path.as_ref());
    if (!rp.fs || !rp.fs->exists(rp.path)) {
        throw Error(keel::format("audioinfo: failed to read '{}' — file not found", path),
                    0, 0, "audioinfo", "", "numkit:audioinfo:fileNotFound");
    }

    const auto bytes = rp.fs->readFileBytes(rp.path);
    AudioInfo info = audioinfoFromBytes(reinterpret_cast<const keel::u8 *>(bytes.data()), bytes.size());

    auto *mr = ctx.engine->allocator();
    Value s = Value::structure(mr);
    s.field("Filename")          = Value::fromString(path.as_ref(), mr);
    s.field("CompressionMethod") = Value::fromString(info.compressionMethod.as_ref(), mr);
    s.field("NumChannels")        = Value::scalar(keel::as<keel::f64>(info.numChannels), mr);
    s.field("SampleRate")         = Value::scalar(info.sampleRate, mr);
    s.field("TotalSamples")       = Value::scalar(keel::as<keel::f64>(info.totalSamples), mr);
    s.field("Duration")           = Value::scalar(info.duration, mr);
    s.field("BitsPerSample")      = Value::scalar(keel::as<keel::f64>(info.bitsPerSample), mr);
    s.field("BitRate")            = Value::scalar(keel::as<keel::f64>(info.bitRate), mr);
    s.field("Title")              = Value::fromString(info.title.as_ref(), mr);
    s.field("Artist")             = Value::fromString(info.artist.as_ref(), mr);
    s.field("Comment")            = Value::fromString(info.comment.as_ref(), mr);

    outs[0] = keel::move(s);
}

void midiread_reg(keel::ArrayRef<Value> args, keel::usize nargout, keel::MutArrayRef<Value> outs,
                  CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 1, 1, "midiread");
    const keel::String path = args[0].toString();
    auto rp = ctx.engine->resolvePath(path.as_ref());
    if (!rp.fs || !rp.fs->exists(rp.path)) {
        throw Error(keel::format("midiread: file not found: {}", path),
                    0, 0, "midiread", "", "numkit:midiread:fileNotFound");
    }

    const auto bytes = rp.fs->readFileBytes(rp.path);
    outs[0] = readMidi(reinterpret_cast<const keel::u8 *>(bytes.data()), bytes.size(), ctx.engine->allocator());

    if (nargout >= 2 && outs.size() >= 2) {
        MidiInfo mi = peekMidi(reinterpret_cast<const keel::u8 *>(bytes.data()), bytes.size());
        auto *mr = ctx.engine->allocator();
        Value s = Value::structure(mr);
        s.field("Format")               = Value::scalar(keel::as<keel::f64>(mi.format), mr);
        s.field("NumTracks")            = Value::scalar(keel::as<keel::f64>(mi.numTracks), mr);
        s.field("TicksPerQuarterNote")  = Value::scalar(keel::as<keel::f64>(mi.ticksPerQuarterNote), mr);
        s.field("InitialTempoBpm")      = Value::scalar(mi.initialTempoBpm, mr);
        s.field("Duration")             = Value::scalar(mi.duration, mr);
        s.field("TotalNotes")           = Value::scalar(keel::as<keel::f64>(mi.totalNotes), mr);
        s.field("TrackName")            = Value::fromString(mi.trackName.as_ref(), mr);
        outs[1] = keel::move(s);
    }
}

void midiwrite_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/, keel::MutArrayRef<Value> /*outs*/,
                   CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 2, 4, "midiwrite");
    const keel::String path = args[0].toString();
    const Value &notes = args[1];

    keel::u16 tpqn = 480;
    keel::f64 tempo = 120.0;
    if (args.size() >= 3 && !args[2].isEmpty()) {
        if (!args[2].isNumeric() || args[2].isComplex() || args[2].numel() != 1) {
            throw Error("midiwrite: ticks per quarter note must be a positive real numeric scalar",
                        0, 0, "midiwrite", "", "numkit:midiwrite:tpqn");
        }
        const keel::f64 tpqnVal = args[2].elemAsDouble(0);
        if (!keel::is_finite(tpqnVal) || !keel::is_integer(tpqnVal) || tpqnVal < 1.0 || tpqnVal > 65535.0) {
            throw Error("midiwrite: ticks per quarter note must be an integer in [1, 65535]",
                        0, 0, "midiwrite", "", "numkit:midiwrite:tpqn");
        }
        tpqn = keel::as<keel::u16>(tpqnVal);
    }
    if (args.size() >= 4 && !args[3].isEmpty()) {
        if (!args[3].isNumeric() || args[3].isComplex() || args[3].numel() != 1) {
            throw Error("midiwrite: tempo must be a positive real numeric scalar",
                        0, 0, "midiwrite", "", "numkit:midiwrite:tempo");
        }
        const keel::f64 tempoVal = args[3].elemAsDouble(0);
        if (!keel::is_finite(tempoVal) || !(tempoVal > 0.0)) {
            throw Error("midiwrite: tempo must be finite and positive",
                        0, 0, "midiwrite", "", "numkit:midiwrite:tempo");
        }
        tempo = tempoVal;
    }

    auto bytes = writeMidiToBytes(notes, tpqn, tempo);
    auto rp = ctx.engine->resolvePath(path.as_ref());
    if (!rp.fs) {
        throw Error(keel::format("midiwrite: no filesystem for '{}'", path),
                    0, 0, "midiwrite", "", "numkit:midiwrite:fs");
    }
    rp.fs->writeFileBytes(rp.path, keel::StringRef(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

void midiinfo_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/, keel::MutArrayRef<Value> outs,
                  CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 1, 1, "midiinfo");
    const keel::String path = args[0].toString();
    auto rp = ctx.engine->resolvePath(path.as_ref());
    if (!rp.fs || !rp.fs->exists(rp.path)) {
        throw Error(keel::format("midiinfo: file not found: {}", path),
                    0, 0, "midiinfo", "", "numkit:midiinfo:fileNotFound");
    }

    const auto bytes = rp.fs->readFileBytes(rp.path);
    MidiInfo mi = peekMidi(reinterpret_cast<const keel::u8 *>(bytes.data()), bytes.size());

    auto *mr = ctx.engine->allocator();
    Value s = Value::structure(mr);
    s.field("Format")               = Value::scalar(keel::as<keel::f64>(mi.format), mr);
    s.field("NumTracks")            = Value::scalar(keel::as<keel::f64>(mi.numTracks), mr);
    s.field("TicksPerQuarterNote")  = Value::scalar(keel::as<keel::f64>(mi.ticksPerQuarterNote), mr);
    s.field("InitialTempoBpm")      = Value::scalar(mi.initialTempoBpm, mr);
    s.field("Duration")             = Value::scalar(mi.duration, mr);
    s.field("TotalNotes")           = Value::scalar(keel::as<keel::f64>(mi.totalNotes), mr);
    s.field("TrackName")            = Value::fromString(mi.trackName.as_ref(), mr);

    outs[0] = keel::move(s);
}

} // namespace numkit::audio::detail
