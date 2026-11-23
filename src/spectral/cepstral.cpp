// toolboxes/audio/src/spectral/cepstral.cpp
// Cepstral coefficients (cepstralCoefficients / mfcc / gtcc), delegated to keel::audio.

#include <keel/arena.h>
#include <keel/tuple.h>
#include <keel/math.h>
#include <keel/array.h>
#include <keel/audio.h>
#include <keel/core.h>
#include <keel/format.h>
#include <keel/memory.h>
#include <numkit/audio/spectral/cepstral.hpp>
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>
#include <numkit/value/value.hpp>


namespace numkit::audio {

namespace {

keel::audio::CepstralRectification toKeelRect(CepstralRect rect) noexcept
{
    switch (rect) {
        case CepstralRect::Log:       return keel::audio::CepstralRectification::kLog;
        case CepstralRect::CubicRoot: return keel::audio::CepstralRectification::kCubicRoot;
        case CepstralRect::None:      return keel::audio::CepstralRectification::kNone;
    }
    return keel::audio::CepstralRectification::kLog;
}

void cepstralCoefficients_argcheck(const Value &S, keel::i32 numCoeffs, CepstralRect rect)
{
    if (S.type() == ValueType::COMPLEX)
        throw Error("cepstralCoefficients: input 1 (S) must be real.",
                    0, 0, "cepstralCoefficients", "",
                    "numkit:cepstralCoefficients:expectedReal");
    if (S.type() != ValueType::DOUBLE && S.type() != ValueType::SINGLE
        && S.type() != ValueType::EMPTY)
        throw Error(keel::format("cepstralCoefficients: input 1 (S) must be double or "
                                 "single (got a {} array).", mtypeName(S.type())),
                    0, 0, "cepstralCoefficients", "",
                    "numkit:cepstralCoefficients:invalidType");
    if (numCoeffs <= 1)
        throw Error(keel::format("cepstralCoefficients: NumCoeffs must be an integer "
                                 "greater than 1 (got {}).", numCoeffs),
                    0, 0, "cepstralCoefficients", "",
                    "numkit:cepstralCoefficients:BadN");
    if (rect != CepstralRect::None) {
        for (keel::usize i = 0; i < S.numel(); ++i)
            if (!(S.elemAsDouble(i) >= 0.0))
                throw Error(keel::format("cepstralCoefficients: input 1 (S) must be "
                                         "nonnegative for the '{}' rectification (got a negative value).",
                                         rect == CepstralRect::CubicRoot ? "cubic-root" : "log"),
                            0, 0, "cepstralCoefficients", "",
                            "numkit:cepstralCoefficients:expectedNonnegative");
    }
}

void mfcc_argcheck(const Value &x, keel::f64 fs, keel::i32 numCoeffs)
{
    (void)x;
    if (!(fs > 0.0) || !keel::is_finite(fs))
        throw Error("mfcc: fs must be a finite positive scalar",
                    0, 0, "mfcc", "", "numkit:mfcc:expectedPositive");
    if (numCoeffs <= 1)
        throw Error(keel::format("mfcc: NumCoeffs must be an integer greater than 1 (got {}).",
                                 numCoeffs),
                    0, 0, "mfcc", "", "numkit:mfcc:BadN");
}

void gtcc_argcheck(const Value &x, keel::f64 fs, keel::i32 numCoeffs)
{
    (void)x;
    if (!(fs > 0.0) || !keel::is_finite(fs))
        throw Error("gtcc: fs must be a finite positive scalar",
                    0, 0, "gtcc", "", "numkit:gtcc:expectedPositive");
    if (numCoeffs <= 1)
        throw Error(keel::format("gtcc: NumCoeffs must be an integer greater than 1 (got {}).",
                                 numCoeffs),
                    0, 0, "gtcc", "", "numkit:gtcc:BadN");
}

} // anon

Value cepstralCoefficients(const Value &S, keel::i32 numCoeffs, CepstralRect rect,
                           keel::Allocator *mr)
{
    cepstralCoefficients_argcheck(S, numCoeffs, rect);

    const bool wantSingle = S.type() == ValueType::SINGLE;
    const Dims &d = S.dims();
    const keel::usize L = d.dim(0);
    const keel::usize K = keel::as<keel::usize>(numCoeffs);

    keel::Array<keel::usize> od;
    od.push_back(d.dim(1));
    od.push_back(K);
    for (keel::i32 k = 2; k < d.ndim(); ++k) od.push_back(d.dim(k));
    Value out;
    if (od.size() == 2)
        out = Value::matrix(od[0], od[1], wantSingle ? ValueType::SINGLE
                                                     : ValueType::DOUBLE, mr);
    else
        out = Value::matrixND(od.data(), keel::as<keel::i32>(od.size()),
                              wantSingle ? ValueType::SINGLE
                                         : ValueType::DOUBLE, mr);
    const keel::usize pageFrames = d.dim(1);
    if (L == 0 || pageFrames == 0 || S.numel() == 0) return out;

    const keel::usize numPages = S.numel() / (L * pageFrames);
    const keel::usize pageIn = L * pageFrames;
    const keel::usize pageOut = pageFrames * K;
    const auto kRect = toKeelRect(rect);

    if (wantSingle) {
        const keel::f32 *sd = S.singleData();
        keel::f32 *od_f = out.singleDataMut();
        for (keel::usize pg = 0; pg < numPages; ++pg) {
            (void)keel::audio::cepstral_coefficients_f32(
                keel::ArrayRef<keel::f32>(sd + pg * pageIn, pageIn),
                L, pageFrames, K,
                keel::MutArrayRef<keel::f32>(od_f + pg * pageOut, pageOut),
                kRect, mr);
        }
    } else {
        const keel::f64 *sd = S.doubleData();
        keel::f64 *od_d = out.doubleDataMut();
        for (keel::usize pg = 0; pg < numPages; ++pg) {
            (void)keel::audio::cepstral_coefficients(
                keel::ArrayRef<keel::f64>(sd + pg * pageIn, pageIn),
                L, pageFrames, K,
                keel::MutArrayRef<keel::f64>(od_d + pg * pageOut, pageOut),
                kRect, mr);
        }
    }
    return out;
}

keel::Tuple<Value, Value, Value>
mfcc(const Value &x, keel::f64 fs, keel::i32 numCoeffs, keel::Allocator *mr)
{
    mfcc_argcheck(x, fs, numCoeffs);
    const keel::usize N = x.numel();
    const keel::usize numFrames = keel::audio::audio_frame_count(N, fs, 0.03, 0.02);
    const keel::usize NC = keel::as<keel::usize>(numCoeffs);

    Value out = Value::matrix(numFrames, numFrames == 0 ? 0 : (NC + 1),
                              ValueType::DOUBLE, mr);
    Value zero = Value::matrix(0, 0, ValueType::DOUBLE, mr);
    if (numFrames == 0)
        return {out, zero, zero};

    Value delta      = Value::matrix(numFrames, NC + 1, ValueType::DOUBLE, mr);
    Value deltaDelta = Value::matrix(numFrames, NC + 1, ValueType::DOUBLE, mr);

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

    const keel::usize total = numFrames * (NC + 1);
    (void)keel::audio::mfcc(
        xRef, fs, NC,
        keel::MutArrayRef<keel::f64>(out.doubleDataMut(), total),
        keel::MutArrayRef<keel::f64>(delta.doubleDataMut(), total),
        keel::MutArrayRef<keel::f64>(deltaDelta.doubleDataMut(), total),
        mr);
    return {out, delta, deltaDelta};
}

keel::Tuple<Value, Value, Value>
gtcc(const Value &x, keel::f64 fs, keel::i32 numCoeffs, keel::Allocator *mr)
{
    gtcc_argcheck(x, fs, numCoeffs);
    const keel::usize N = x.numel();
    const keel::usize numFrames = keel::audio::audio_frame_count(N, fs, 0.03, 0.02);
    const keel::usize NC = keel::as<keel::usize>(numCoeffs);
    const keel::f64 diffErb = keel::ceil(keel::audio::hz_to_erb(fs * 0.5) - keel::audio::hz_to_erb(50.0));
    const keel::usize numBands = (diffErb > 0.0) ? keel::as<keel::usize>(diffErb) : 0;

    Value out = Value::matrix(numFrames, numFrames == 0 ? 0 : (NC + 1),
                              ValueType::DOUBLE, mr);
    Value zero = Value::matrix(0, 0, ValueType::DOUBLE, mr);
    if (numFrames == 0 || numBands == 0)
        return {out, zero, zero};

    Value delta      = Value::matrix(numFrames, NC + 1, ValueType::DOUBLE, mr);
    Value deltaDelta = Value::matrix(numFrames, NC + 1, ValueType::DOUBLE, mr);

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

    const keel::usize total = numFrames * (NC + 1);
    (void)keel::audio::gtcc(
        xRef, fs, NC,
        keel::MutArrayRef<keel::f64>(out.doubleDataMut(), total),
        keel::MutArrayRef<keel::f64>(delta.doubleDataMut(), total),
        keel::MutArrayRef<keel::f64>(deltaDelta.doubleDataMut(), total),
        mr);
    return {out, delta, deltaDelta};
}

} // namespace numkit::audio
