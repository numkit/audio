// toolboxes/audio/src/spectral/melspec_delta.cpp
//
// Audio Cycle C — melSpectrogram + audioDelta, delegated to keel::audio.

#include <keel/core.h>
#include <keel/tuple.h>
#include <keel/math.h>
#include <keel/memory.h>
#include <keel/arena.h>
#include <keel/array.h>
#include <keel/audio.h>
#include <keel/format.h>
#include <numkit/audio/spectral/melspec_delta.hpp>

#include <numkit/value/argcheck.hpp>
#include <numkit/value/value.hpp>
#include <numkit/value/error.hpp>

#include "fft_one_sided.hpp"


namespace numkit::audio {

namespace {

void melSpectrogram_argcheck(const Value &x, keel::f64 fs, keel::i32 numBands)
{
    (void)x;
    if (!(fs > 0.0) || !keel::is_finite(fs))
        throw Error("melSpectrogram: fs must be a finite positive scalar",
                    0, 0, "melSpectrogram", "", "numkit:melSpectrogram:expectedPositive");
    if (numBands < 1)
        throw Error("melSpectrogram: NumBands must be a positive integer",
                    0, 0, "melSpectrogram", "", "numkit:melSpectrogram:expectedPositiveInteger");
}

void audioDelta_argcheck(const Value &x, keel::i32 windowLength)
{
    if (windowLength < 3 || (windowLength % 2) == 0)
        throw Error(keel::format("audioDelta: windowLength must be an odd integer "
                                 "greater than 1 (got {}).", windowLength),
                    0, 0, "audioDelta", "", "numkit:audioDelta:BadWin");

    if (x.type() == ValueType::COMPLEX)
        throw Error("audioDelta: input 1 (x) must be real.",
                    0, 0, "audioDelta", "", "numkit:audioDelta:expectedReal");
    if (x.type() != ValueType::DOUBLE && x.type() != ValueType::SINGLE
        && x.type() != ValueType::EMPTY)
        throw Error(keel::format("audioDelta: input 1 (x) must be double or single "
                                 "(got a {} array).", mtypeName(x.type())),
                    0, 0, "audioDelta", "", "numkit:audioDelta:invalidType");
}

} // anon

keel::Tuple<Value, Value, Value>
melSpectrogram(const Value &x, keel::f64 fs, keel::i32 numBands,
               keel::Allocator *mr)
{
    melSpectrogram_argcheck(x, fs, numBands);
    const keel::usize N = x.numel();
    const keel::usize winLen  = keel::as<keel::usize>(keel::round(fs * 0.03));
    const keel::usize overlap = keel::as<keel::usize>(keel::round(fs * 0.02));
    const keel::usize hop = winLen - overlap;
    const keel::usize fftLen = winLen;
    const keel::usize H = fftLen / 2 + 1;
    const keel::usize numFrames = (N >= winLen) ? ((N - winLen) / hop + 1) : 0;
    const keel::usize M = keel::as<keel::usize>(numBands);

    Value S = Value::matrix(M, numFrames, ValueType::DOUBLE, mr);
    Value F = Value::matrix(1, M, ValueType::DOUBLE, mr);
    Value T = Value::matrix(numFrames, numFrames == 0 ? 0 : 1, ValueType::DOUBLE, mr);

    if (numFrames == 0 || winLen == 0 || M == 0) return {S, F, T};

    keel::ArenaTemp scratch = keel::get_scratch(mr);
    keel::Array<keel::f64> win(winLen, scratch.allocator());
    keel::audio::hamming_periodic(win);

    // WindowNormalization (power form): win /= sqrt(0.5 * sum(win)^2)
    keel::f64 sumW = 0.0;
    for (keel::usize i = 0; i < winLen; ++i) sumW += win[i];
    const keel::f64 scale = keel::sqrt(0.5 * sumW * sumW);
    if (scale > 0.0)
        for (keel::usize i = 0; i < winLen; ++i) win[i] /= scale;

    // Filterbank via keel::audio::mel_filterbank
    keel::Array<keel::f64> fb(M * H, scratch.allocator());
    (void)keel::audio::mel_filterbank(fs, M, fftLen, fb, {F.doubleDataMut(), M});

    // Time vector.
    keel::f64 *td = T.doubleDataMut();
    for (keel::usize f = 0; f < numFrames; ++f)
        td[f] = (keel::as<keel::f64>(f * hop) + keel::as<keel::f64>(winLen) / 2.0) / fs;

    // Per-frame: window, FFT, power, mel-filterbank.
    keel::Array<keel::f64> frame(winLen, scratch.allocator());
    keel::Array<keel::f64> pow_half(H, scratch.allocator());
    keel::f64 *Sd = S.doubleDataMut();
    for (keel::usize f = 0; f < numFrames; ++f) {
        const keel::usize start = f * hop;
        for (keel::usize i = 0; i < winLen; ++i)
            frame[i] = x.elemAsDouble(start + i) * win[i];
        detail::fftPowerHalf(mr, frame.data(), winLen, pow_half.data());
        for (keel::usize b = 0; b < M; ++b) {
            keel::f64 s = 0.0;
            for (keel::usize k = 0; k < H; ++k) s += fb[b + k * M] * pow_half[k];
            Sd[b + f * M] = s;
        }
    }
    return {S, F, T};
}

Value audioDeltaFull(const Value &x, keel::i32 windowLength,
                     const Value *initialCondition, Value *finalCondition,
                     keel::Allocator *mr)
{
    audioDelta_argcheck(x, windowLength);

    const keel::usize L = keel::as<keel::usize>(windowLength);
    const keel::usize stateLen = L - 1;
    const bool wantSingle = x.type() == ValueType::SINGLE;
    const Dims &d = x.dims();
    const keel::usize R = d.dim(0);
    keel::usize C = 1;
    for (keel::i32 k = 1; k < d.ndim(); ++k) C *= d.dim(k);
    Value out;
    if (d.ndim() <= 2)
        out = Value::matrix(R, C, wantSingle ? ValueType::SINGLE
                                             : ValueType::DOUBLE, mr);
    else {
        keel::Array<keel::usize> od(d.ndim());
        od[0] = R;
        for (keel::i32 k = 1; k < d.ndim(); ++k) od[k] = d.dim(k);
        out = Value::matrixND(od.data(), keel::as<keel::i32>(od.size()),
                              wantSingle ? ValueType::SINGLE
                                         : ValueType::DOUBLE, mr);
    }
    if (R == 0 || C == 0) return out;

    const bool haveZi = initialCondition && !initialCondition->isEmpty();
    const bool singleState = haveZi && initialCondition->numel() == stateLen;
    if (haveZi) {
        if (!singleState
            && (initialCondition->dims().dim(0) != stateLen
                || initialCondition->numel() != stateLen * C))
            throw Error(keel::format("audioDelta: initialCondition must have {} rows and {} channels.",
                                     stateLen, C),
                        0, 0, "audioDelta", "",
                        "numkit:audioDelta:BadInitialConditions");
        if (initialCondition->type() == ValueType::COMPLEX)
            throw Error("audioDelta: initialCondition must be real.",
                        0, 0, "audioDelta", "",
                        "numkit:audioDelta:BadInitialConditions");
    }

    keel::ArenaTemp scratch = keel::get_scratch(mr);
    keel::Array<keel::f64> colIn(R, scratch.allocator());
    keel::Array<keel::f64> colOut(R, scratch.allocator());
    keel::Array<keel::f64> ziCol(haveZi ? stateLen : 0, scratch.allocator());
    keel::Array<keel::f64> zfAll(finalCondition ? (stateLen * C) : 0, scratch.allocator());

    keel::f64 *od = wantSingle ? nullptr : out.doubleDataMut();
    keel::f32 *of = wantSingle ? out.singleDataMut() : nullptr;

    for (keel::usize c = 0; c < C; ++c) {
        for (keel::usize n = 0; n < R; ++n) colIn[n] = x.elemAsDouble(n + c * R);
        if (haveZi) {
            for (keel::usize i = 0; i < stateLen; ++i) {
                ziCol[i] = singleState ? initialCondition->elemAsDouble(i)
                                       : initialCondition->elemAsDouble(c * stateLen + i);
            }
        }
        keel::MutArrayRef<keel::f64> zfCol =
            finalCondition ? keel::MutArrayRef<keel::f64>(zfAll.data() + c * stateLen, stateLen)
                           : keel::MutArrayRef<keel::f64>{};
        (void)keel::audio::audio_delta(colIn, L, colOut, ziCol, zfCol, mr);

        if (of) {
            for (keel::usize n = 0; n < R; ++n) of[n + c * R] = keel::as<keel::f32>(colOut[n]);
        } else {
            for (keel::usize n = 0; n < R; ++n) od[n + c * R] = colOut[n];
        }
    }

    if (finalCondition) {
        *finalCondition = Value::matrix(stateLen, C,
                                        wantSingle ? ValueType::SINGLE
                                                   : ValueType::DOUBLE, mr);
        keel::f64 *fd = wantSingle ? nullptr : finalCondition->doubleDataMut();
        keel::f32 *ff = wantSingle ? finalCondition->singleDataMut() : nullptr;
        for (keel::usize i = 0; i < stateLen * C; ++i) {
            if (ff) ff[i] = keel::as<keel::f32>(zfAll[i]);
            else    fd[i] = zfAll[i];
        }
    }
    return out;
}

Value audioDelta(const Value &x, keel::i32 windowLength, keel::Allocator *mr)
{
    return audioDeltaFull(x, windowLength, nullptr, nullptr, mr);
}

} // namespace numkit::audio
