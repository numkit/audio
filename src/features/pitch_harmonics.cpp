// toolboxes/audio/src/features/pitch_harmonics.cpp
// Pitch (NCF, CEP, PEF, LHS, SRH) and harmonicRatio, delegated to keel::audio.

#include <keel/arena.h>
#include <keel/math.h>
#include <keel/array.h>
#include <keel/audio.h>
#include <keel/core.h>
#include <keel/memory.h>
#include <numkit/audio/features/pitch_harmonics.hpp>
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>
#include <numkit/value/value.hpp>


namespace numkit::audio {

namespace {

void pitch_argcheck(const Value &x, keel::f64 fs, keel::f64 minF, keel::f64 maxF)
{
    (void)x;
    if (!(fs > 0.0) || !keel::is_finite(fs))
        throw Error("pitch: fs must be a finite positive scalar",
                    0, 0, "pitch", "", "numkit:pitch:expectedPositive");
    if (!(minF > 0.0 && maxF > minF && keel::is_finite(minF) && keel::is_finite(maxF)))
        throw Error("pitch: Range must satisfy 0 < Range(1) < Range(2)",
                    0, 0, "pitch", "", "numkit:pitch:BadRange");
}

void harmonicRatio_argcheck(const Value &x, keel::f64 fs)
{
    (void)x;
    if (!(fs > 0.0) || !keel::is_finite(fs))
        throw Error("harmonicRatio: fs must be a finite positive scalar",
                    0, 0, "harmonicRatio", "", "numkit:harmonicRatio:expectedPositive");
}

Value runPitch(const Value &x, keel::f64 fs, keel::f64 minF, keel::f64 maxF,
               keel::audio::PitchMethod method, keel::Allocator *mr)
{
    pitch_argcheck(x, fs, minF, maxF);
    const keel::usize N = x.numel();
    const keel::usize numFrames = keel::audio::audio_frame_count(N, fs, 0.052, 0.042);
    if (numFrames == 0)
        return Value::matrix(0, 0, ValueType::DOUBLE, mr);

    keel::ArenaTemp scratch = keel::get_scratch(mr);
    keel::Array<keel::f64> xBuf(scratch.allocator());
    keel::ArrayRef<keel::f64> xRef;
    if (x.type() == ValueType::DOUBLE) {
        xRef = keel::ArrayRef<keel::f64>(x.doubleData(), N);
    } else {
        xBuf.resize(N);
        for (keel::usize i = 0; i < N; ++i) xBuf[i] = x.elemAsDouble(i);
        xRef = xBuf;
    }

    Value out = Value::matrix(numFrames, 1, ValueType::DOUBLE, mr);
    const auto st = keel::audio::pitch(
        xRef, fs, keel::MutArrayRef<keel::f64>(out.doubleDataMut(), numFrames),
        minF, maxF, method, mr);
    if (st != keel::audio::AudioStatus::kOk)
        return Value::matrix(0, 0, ValueType::DOUBLE, mr);
    return out;
}

} // anon

Value pitchCEP(const Value &x, keel::f64 fs, keel::f64 minF, keel::f64 maxF, keel::Allocator *mr)
{
    return runPitch(x, fs, minF, maxF, keel::audio::PitchMethod::kCep, mr);
}

Value pitchPEF(const Value &x, keel::f64 fs, keel::f64 minF, keel::f64 maxF, keel::Allocator *mr)
{
    return runPitch(x, fs, minF, maxF, keel::audio::PitchMethod::kPef, mr);
}

Value pitchLHS(const Value &x, keel::f64 fs, keel::f64 minF, keel::f64 maxF, keel::Allocator *mr)
{
    return runPitch(x, fs, minF, maxF, keel::audio::PitchMethod::kLhs, mr);
}

Value pitchSRH(const Value &x, keel::f64 fs, keel::f64 minF, keel::f64 maxF, keel::Allocator *mr)
{
    return runPitch(x, fs, minF, maxF, keel::audio::PitchMethod::kSrh, mr);
}

Value pitch(const Value &x, keel::f64 fs, keel::f64 minF, keel::f64 maxF, keel::Allocator *mr)
{
    return runPitch(x, fs, minF, maxF, keel::audio::PitchMethod::kNcf, mr);
}

Value harmonicRatio(const Value &x, keel::f64 fs, keel::Allocator *mr)
{
    harmonicRatio_argcheck(x, fs);
    const keel::usize N = x.numel();
    const keel::usize numFrames = keel::audio::audio_frame_count(N, fs, 0.03, 0.02);
    if (numFrames == 0)
        return Value::matrix(0, 0, ValueType::DOUBLE, mr);

    keel::ArenaTemp scratch = keel::get_scratch(mr);
    keel::Array<keel::f64> xBuf(scratch.allocator());
    keel::ArrayRef<keel::f64> xRef;
    if (x.type() == ValueType::DOUBLE) {
        xRef = keel::ArrayRef<keel::f64>(x.doubleData(), N);
    } else {
        xBuf.resize(N);
        for (keel::usize i = 0; i < N; ++i) xBuf[i] = x.elemAsDouble(i);
        xRef = xBuf;
    }

    Value out = Value::matrix(numFrames, 1, ValueType::DOUBLE, mr);
    (void)keel::audio::harmonic_ratio(
        xRef, fs, keel::MutArrayRef<keel::f64>(out.doubleDataMut(), numFrames), mr);
    return out;
}

} // namespace numkit::audio
