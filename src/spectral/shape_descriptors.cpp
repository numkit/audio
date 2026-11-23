// toolboxes/audio/src/spectral/shape_descriptors.cpp
//
// Audio Toolbox spectral shape descriptors. Audio Cycle B.
//
//   spectralCentroid       Σ(f·X) / Σ(X) — first moment
//   spectralSpread         sqrt(Σ((f-c)²·X) / Σ(X)) — std around centroid
//   spectralRolloffPoint   freq below which P% of energy contained (P=0.95)
//   spectralDecrease       (1/Σk≥2 X(k)) · Σk≥2 (X(k)-X(1))/(k-1)
//   spectralSlope          regression slope of log10(X+eps) vs f
//   spectralFlux           (Σ |X_t - X_{t-1}|^p)^(1/p)  (p default 2)
//
// Each function accepts either:
//   (x, fs)  — x is real column vector signal, fs is scalar sample rate.
//              Internal STFT: window=rectwin(round(0.03*fs)), overlap
//              =round(0.02*fs), FFTLength=winLen. Returns per-frame col.
//   (X, F)   — X is power-spectrum matrix (M-by-N, one column per frame),
//              F is M-element frequency vector. Returns 1-by-N row of
//              per-column metric values.
//
// PMR HARD RULE: every fn takes std::pmr::memory_resource *mr.

#include <keel/core.h>
#include <keel/memory.h>
#include <keel/arena.h>
#include <keel/array.h>
#include <keel/format.h>
#include <keel/math.h>
#include <keel/audio.h>
#include <numkit/audio/spectral/shape_descriptors.hpp>

// Compute-only TU: Value substrate + Error, no engine. The spectral*
// builtins (CallContext wrappers) live in spectral/shape_descriptors_reg.cpp.
#include <numkit/value/argcheck.hpp>
#include <numkit/value/value.hpp>
#include <numkit/value/error.hpp>

#include "fft_one_sided.hpp"

#include <algorithm>


namespace numkit::audio {

namespace {

struct Stft {
    Value X;              // M × (F·C), channel-major (channel c's frames
                          // occupy columns [c·F, c·F+F)); M = selected bins
    Value F;              // M × 1, frequency axis in Hz
    keel::usize numChan = 1;
    keel::usize numFrames = 0;
};

// Shared argument validation for the spectral-descriptor options, in the
// reference's check order (window first, then overlap, FFT length,
// range) — the blame follows the first failing argument.
static void requireFloatClass(const Value &v, const char *fn,
                              const char *argName, keel::i32 argNum)
{
    if (v.type() == ValueType::SINGLE || v.type() == ValueType::DOUBLE
        || v.type() == ValueType::COMPLEX || v.type() == ValueType::EMPTY)
        return;
    throw Error(keel::format("{}: input {} ({}) must be single or double, not {}.",
                             fn, argNum, argName, mtypeName(v.type())),
                0, 0, fn, "", keel::format("numkit:{}:invalidType", fn));
}

// Build the STFT (short-time Fourier transform) power/magnitude matrix
// for every channel of x. Exact port of the reference pipeline
// (signal.internal.spectraldescriptors preprocess/stft, R2025b):
//
//   window  — no user window: round(0.03·fs) > 120 → rectwin of that
//             length (must fit the signal); otherwise, with no overlap
//             option rectwin(min(64, xLen)); with an OverlapLength
//             option rectwin(fix((xLen + 7·ovl)/8)) (the "8 segments"
//             geometry — probed: every OverlapLength-only call at
//             fs=1600/N=160 and fs=4000/N=1000 frames into 8 hops).
//             A user window must be a real finite single/double COLUMN
//             of length 3..xLen.
//   overlap — no option: round(0.02·fs) > 80 → that, else half the
//             window; an option must be a single/double scalar integer
//             ≥ 0 and < windowLength.
//   nfft    — option or window length; ≥ 2 and ≥ windowLength.
//   range   — option or [0 floor(fs/2)]; row, increasing, in [0, fs/2].
//   bins    — ceil(R(1)·nfft/fs+1) .. floor(R(2)·nfft/fs+1), power
//             |Y|²/(0.5·sum(win)²) or magnitude |Y|/(0.5·sum(win)), DC
//             halved when selected, Nyquist halved when selected and
//             nfft even.
//
// Default window thresholds re-probed from the reference source: the
// earlier ≥128 / pinned-64/32 reading matched the probed fs grid but
// the real thresholds are >120 / >80 with min(64, xLen) and half-window
// fallbacks.
static Stft computeStft(keel::Allocator *mr, const Value &x,
                        keel::f64 fs, bool magnitude, const char *fn,
                        const StftOptions &opts)
{
    const keel::usize xLen   = x.dims().rows();
    const keel::usize numChan = x.dims().cols();
    const bool haveWin = !opts.Window.isEmpty();
    const bool haveOvl = !opts.OverlapLength.isEmpty();

    // ── window ──
    keel::Array<keel::f64> win;
    keel::usize winLen;
    if (!haveWin) {
        const keel::f64 wNatural = keel::round(fs * 0.03);
        if (wNatural > 120.0) {
            if (wNatural > keel::as<keel::f64>(xLen))
                throw Error(keel::format("{}: the default window length round(0.03·f) = {} exceeds the signal length {}.",
                                         fn, keel::as<keel::i64>(wNatural), xLen),
                            0, 0, fn, "",
                            keel::format("numkit:{}:BadWindowLength", fn));
            winLen = keel::as<keel::usize>(wNatural);
            win.assign(winLen, 1.0);
        } else if (!haveOvl) {
            winLen = std::min<keel::usize>(64, xLen);
            win.assign(winLen, 1.0);
        } else {
            // "8 segments" geometry: window sized so that 8 hops of
            // (window − ovl) tile the signal exactly.
            const keel::f64 ov0 = opts.OverlapLength.elemAsDouble(0);
            winLen = keel::as<keel::usize>(
                keel::floor((keel::as<keel::f64>(xLen) + 7.0 * ov0) / 8.0));
            win.assign(winLen, 1.0);
        }
    } else {
        const Value &W = opts.Window;
        requireFloatClass(W, fn, "Window", 3);
        if (W.type() == ValueType::COMPLEX)
            throw Error(keel::format("{}: input Window must be real.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedReal", fn));
        if (W.numel() == 0)
            throw Error(keel::format("{}: input Window must be nonempty.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedNonempty", fn));
        if (W.dims().cols() != 1 && W.numel() > 1)
            throw Error(keel::format("{}: input Window must be a column vector.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedColumn", fn));
        const keel::usize wn = W.numel();
        for (keel::usize i = 0; i < wn; ++i)
            if (!keel::is_finite(W.elemAsDouble(i)))
                throw Error(keel::format("{}: input Window must be finite.", fn),
                            0, 0, fn, "",
                            keel::format("numkit:{}:expectedFinite", fn));
        if (wn <= 2 || wn > xLen)
            throw Error(keel::format("{}: numel(Window) must be greater than 2 and at most the signal length {} (got {}).",
                                     fn, xLen, wn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:notLessEqual", fn));
        winLen = wn;
        win.resize(wn);
        for (keel::usize i = 0; i < wn; ++i) win[i] = W.elemAsDouble(i);
    }

    // ── overlap ──
    keel::usize overlap;
    if (!haveOvl) {
        const keel::f64 oNat = keel::round(fs * 0.02);
        overlap = (oNat > 80.0) ? keel::as<keel::usize>(oNat)
                                : winLen / 2;      // fix(0.5·windowLength)
    } else {
        const Value &O = opts.OverlapLength;
        requireFloatClass(O, fn, "OverlapLength", 3);
        if (O.type() == ValueType::COMPLEX)
            throw Error(keel::format("{}: input OverlapLength must be real.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedReal", fn));
        if (O.numel() != 1)
            throw Error(keel::format("{}: input OverlapLength must be a scalar.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedScalar", fn));
        const keel::f64 v = O.elemAsDouble(0);
        if (!(v == keel::floor(v)))
            throw Error(keel::format("{}: input OverlapLength must be an integer.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedInteger", fn));
        if (v < 0.0)
            throw Error(keel::format("{}: input OverlapLength must be nonnegative.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedNonnegative", fn));
        if (v >= keel::as<keel::f64>(winLen))
            throw Error(keel::format("{}: OverlapLength must be smaller than the window length {}.",
                                     fn, winLen),
                        0, 0, fn, "",
                        keel::format("numkit:{}:BadOverlapLength", fn));
        overlap = keel::as<keel::usize>(v);
    }

    // ── FFT length ──
    keel::usize nfft = winLen;
    if (!opts.FFTLength.isEmpty()) {
        const Value &Nv = opts.FFTLength;
        if (Nv.type() == ValueType::COMPLEX)
            throw Error(keel::format("{}: input FFTLength must be real.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedReal", fn));
        if (Nv.numel() != 1)
            throw Error(keel::format("{}: input FFTLength must be a scalar.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedScalar", fn));
        const keel::f64 v = Nv.elemAsDouble(0);
        if (!(v == keel::floor(v)) || v <= 0.0)
            throw Error(keel::format("{}: input FFTLength must be a positive integer.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:mustBePositive", fn));
        if (v < 2.0)
            throw Error(keel::format("{}: FFTLength must be at least 2.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:mustBeGreaterThanOrEqual", fn));
        nfft = keel::as<keel::usize>(v);
    }
    if (nfft < winLen)
        throw Error(keel::format("{}: the FFT length must be greater than or equal to the window length, {}.",
                                 fn, winLen),
                    0, 0, fn, "",
                    keel::format("numkit:{}:FFTLengthLessThanWindowLength", fn));

    // ── range ──
    keel::f64 rLo = 0.0, rHi;
    if (fs > 1.0) rHi = keel::floor(fs / 2.0); else rHi = fs / 2.0;
    if (!opts.Range.isEmpty()) {
        const Value &R = opts.Range;
        requireFloatClass(R, fn, "Range", 3);
        if (R.type() == ValueType::COMPLEX)
            throw Error(keel::format("{}: input Range must be real.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedReal", fn));
        if (R.numel() != 2 || R.dims().rows() != 1)
            throw Error(keel::format("{}: input Range must be a 1-by-2 row vector.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedRow", fn));
        rLo = R.elemAsDouble(0);
        rHi = R.elemAsDouble(1);
        if (!keel::is_finite(rLo) || !keel::is_finite(rHi))
            throw Error(keel::format("{}: input Range must be finite.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedFinite", fn));
        if (!(rLo < rHi))
            throw Error(keel::format("{}: Expected Range to be strictly increasing.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedIncreasing", fn));
        if (rLo < 0.0)
            throw Error(keel::format("{}: input Range must be nonnegative.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedNonnegative", fn));
        if (rHi > fs / 2.0)
            throw Error(keel::format("{}: Range(2) must be at most fs/2 = {}.",
                                     fn, fs / 2.0),
                        0, 0, fn, "",
                        keel::format("numkit:{}:notLessEqual", fn));
    }

    const keel::usize hop = winLen - overlap;
    const keel::usize numFrames =
        (xLen >= winLen) ? ((xLen - winLen) / hop + 1) : 0;

    // Selected bins (1-based in the reference; 0-based here after −1).
    const keel::usize binLow  = keel::as<keel::usize>(
        keel::ceil(rLo * keel::as<keel::f64>(nfft) / fs + 1.0));
    const keel::usize binHigh = keel::as<keel::usize>(
        keel::floor(rHi * keel::as<keel::f64>(nfft) / fs + 1.0));
    const keel::usize M = (binHigh >= binLow) ? (binHigh - binLow + 1) : 0;

    Stft s;
    s.numChan = numChan;
    s.numFrames = numFrames;
    s.X = Value::matrix(M, numFrames * numChan, ValueType::DOUBLE, mr);
    s.F = Value::matrix(M, 1, ValueType::DOUBLE, mr);
    if (numFrames == 0 || M == 0) return s;

    keel::f64 *fd = s.F.doubleDataMut();
    for (keel::usize k = 0; k < M; ++k)
        fd[k] = (fs / keel::as<keel::f64>(nfft))
                * keel::as<keel::f64>(binLow - 1 + k);
    if ((nfft % 2) != 0 && binHigh == nfft / 2 + 1 && M > 0)
        fd[M - 1] = fs * keel::as<keel::f64>(nfft - 1)
                    / (2.0 * keel::as<keel::f64>(nfft));

    keel::f64 winSum = 0.0;
    for (keel::f64 w : win) winSum += w;
    const keel::f64 invPow = 1.0 / (0.5 * winSum * winSum);
    const keel::f64 magScl = 2.0 / winSum;
    const bool halveDc = (binLow == 1);
    const bool halveNyq = (binHigh == nfft / 2 + 1) && (nfft % 2 == 0);

    keel::ArenaTemp scratch = keel::get_scratch(mr);
    keel::Array<keel::f64> frame(winLen, scratch.allocator());
    keel::Array<keel::f64> half(nfft / 2 + 1, scratch.allocator());
    keel::f64 *Xd = s.X.doubleDataMut();
    for (keel::usize c = 0; c < numChan; ++c) {
        for (keel::usize f = 0; f < numFrames; ++f) {
            const keel::usize start = f * hop;
            for (keel::usize i = 0; i < winLen; ++i)
                frame[i] = x.elemAsDouble(c * xLen + start + i) * win[i];
            detail::fftPowerHalfN(mr, frame.data(), winLen, nfft,
                                  half.data());
            keel::f64 *col = Xd + (c * numFrames + f) * M;
            for (keel::usize k = 0; k < M; ++k) {
                const keel::f64 p = half[binLow - 1 + k];
                col[k] = magnitude ? magScl * keel::sqrt(p) : p * invPow;
            }
            if (halveDc) col[0] *= 0.5;
            if (halveNyq && M > 0) col[M - 1] *= 0.5;
        }
    }
    return s;
}

// Detect (x, fs) vs (X, F) and validate the shared input contract.
// MATLAB rule: if f is scalar, x is time-domain (single/double real 2-D
// nonempty finite matrix, one channel per COLUMN, ≥ 2 samples) and the
// STFT runs internally; if f is a vector, x is frequency-domain (≤ 3-D,
// ≥ 2 rows, one spectrum per column per page) and passes through.
struct InputData {
    Value X;               // time: M × (F·C) channel-major; freq: as given
    Value F;               // M × 1 frequency vector
    bool  isTime = true;   // true when input was (x, fs)
    bool  wantSingle = false;
    keel::usize numChan = 1;
    keel::usize numFrames = 0;
};

InputData prepareInput(keel::Allocator *mr,
                       const Value &x, const Value &f,
                       bool magnitude, const char *fn,
                       const StftOptions &opts)
{
    InputData d;
    d.wantSingle = (x.type() == ValueType::SINGLE);
    if (f.numel() == 1) {
        // Time-domain: shared preprocess contract. f is a validated
        // nonnegative real finite scalar by the time we get here.
        requireFloatClass(x, fn, "x", 1);
        if (x.type() == ValueType::COMPLEX)
            throw Error(keel::format("{}: input 1 (x) must be real.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedReal", fn));
        if (x.isEmpty())
            throw Error(keel::format("{}: input 1 (x) must be nonempty.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expectedNonempty", fn));
        if (x.dims().pages() > 1)
            throw Error(keel::format("{}: Expected x to be two-dimensional.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:expected2D", fn));
        if (x.dims().rows() < 2)
            throw Error(keel::format("{}: Expected the length of the signals in \"x\" to be larger than 1.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:insufficientSignalLength", fn));
        for (keel::usize i = 0; i < x.numel(); ++i)
            if (!keel::is_finite(x.elemAsDouble(i)))
                throw Error(keel::format("{}: input 1 (x) must be finite.", fn),
                            0, 0, fn, "",
                            keel::format("numkit:{}:expectedFinite", fn));
        const keel::f64 fs = f.elemAsDouble(0);
        Stft s = computeStft(mr, x, fs, magnitude, fn, opts);
        d.X = std::move(s.X);
        d.F = std::move(s.F);
        d.isTime = true;
        d.numChan = s.numChan;
        d.numFrames = s.numFrames;
    } else {
        // Frequency-domain: ≤ 3-D, ≥ 2 rows, F matches the rows.
        requireFloatClass(x, fn, "x", 1);
        if (x.dims().rows() < 2)
            throw Error(keel::format("{}: Expected the number of rows of \"x\" to be larger than 1.", fn),
                        0, 0, fn, "",
                        keel::format("numkit:{}:insufficientSignalLength", fn));
        if (f.numel() != x.dims().rows())
            throw Error(keel::format("{}: size(x,1) must equal numel(f) (got {} and {}).",
                                     fn, x.dims().rows(), f.numel()),
                        0, 0, fn, "",
                        keel::format("numkit:{}:BadFrequencyVector", fn));
        d.X = x;
        d.F = f;
        d.isTime = false;
        d.numChan = std::max<keel::usize>(1, x.dims().pages());
        d.numFrames = x.dims().cols();
    }
    return d;
}

// Iterate (frame, channel) columns; emit the metric matrix F×C. Both
// layouts share the arithmetic: column (m, c) starts at (c·F + m)·M —
// the time form's STFT matrix is channel-major by construction, and a
// frequency-domain L×M×C array is column-major already.
template <typename Cb>
Value perFrameChannel(keel::Allocator *mr, const Value &X,
                      const InputData &d, Cb cb)
{
    const keel::usize M = X.dims().rows();
    const keel::usize F = d.numFrames;
    const keel::usize C = d.numChan;
    Value out = Value::matrix(F, C,
                              d.wantSingle ? ValueType::SINGLE
                                           : ValueType::DOUBLE,
                              mr);
    if (M == 0) return out;
    keel::f64 *od = d.wantSingle ? nullptr : out.doubleDataMut();
    keel::f32 *of = d.wantSingle ? out.singleDataMut() : nullptr;
    keel::ArenaTemp scratch = keel::get_scratch(mr);
    keel::Array<keel::f64> col(M, scratch.allocator());
    for (keel::usize c = 0; c < C; ++c) {
        for (keel::usize m = 0; m < F; ++m) {
            const keel::usize base = (c * F + m) * M;
            for (keel::usize i = 0; i < M; ++i) col[i] = X.elemAsDouble(base + i);
            const keel::f64 v = cb(col.data(), M);
            if (of) of[m + c * F] = keel::as<keel::f32>(v);
            else od[m + c * F] = v;
        }
    }
    return out;
}

} // anon

Value spectralFlux(const Value &x, const Value &f, keel::f64 p,
                   keel::Allocator *mr)
{
    return spectralFluxFull(x, f, p, nullptr, nullptr, false, {}, mr);
}

// ── Centroid ──────────────────────────────────────────────────────────
Value spectralCentroid(const Value &x, const Value &f,
                       const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralCentroid", stft);
    const keel::f64 *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_centroid({col, M}, {Fd, M});
    });
}
Value spectralCentroid(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralCentroid(x, f, StftOptions{}, mr);
}

// ── Spread ────────────────────────────────────────────────────────────
Value spectralSpread(const Value &x, const Value &f,
                     const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralSpread", stft);
    const keel::f64 *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_spread({col, M}, {Fd, M});
    });
}
Value spectralSpread(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralSpread(x, f, StftOptions{}, mr);
}

// ── Rolloff point ─────────────────────────────────────────────────────
Value spectralRolloffPoint(const Value &x, const Value &f, keel::f64 percentile,
                           const StftOptions &stft,
                           keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralRolloffPoint", stft);
    const keel::f64 *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd, percentile](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_rolloff_point({col, M}, {Fd, M}, percentile);
    });
}
Value spectralRolloffPoint(const Value &x, const Value &f, keel::f64 percentile,
                           keel::Allocator *mr)
{
    return spectralRolloffPoint(x, f, percentile, StftOptions{}, mr);
}

// ── Decrease ──────────────────────────────────────────────────────────
Value spectralDecrease(const Value &x, const Value &f,
                       const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralDecrease", stft);
    return perFrameChannel(mr, d.X, d, [](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_decrease({col, M});
    });
}
Value spectralDecrease(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralDecrease(x, f, StftOptions{}, mr);
}

// ── Slope ─────────────────────────────────────────────────────────────
Value spectralSlope(const Value &x, const Value &f,
                    const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralSlope", stft);
    const keel::f64 *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_slope({col, M}, {Fd, M});
    });
}
Value spectralSlope(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralSlope(x, f, StftOptions{}, mr);
}

// ── Cycle I: Crest / Entropy / Flatness / Kurtosis / Skewness ─────────

Value spectralCrest(const Value &x, const Value &f,
                    const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralCrest", stft);
    return perFrameChannel(mr, d.X, d, [](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_crest({col, M});
    });
}
Value spectralCrest(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralCrest(x, f, StftOptions{}, mr);
}

Value spectralEntropy(const Value &x, const Value &f,
                      const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralEntropy", stft);
    return perFrameChannel(mr, d.X, d, [](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_entropy({col, M});
    });
}
Value spectralEntropy(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralEntropy(x, f, StftOptions{}, mr);
}

Value spectralFlatness(const Value &x, const Value &f,
                       const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralFlatness", stft);
    return perFrameChannel(mr, d.X, d, [](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_flatness({col, M});
    });
}
Value spectralFlatness(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralFlatness(x, f, StftOptions{}, mr);
}

Value spectralKurtosis(const Value &x, const Value &f,
                       const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralKurtosis", stft);
    const keel::f64 *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_kurtosis({col, M}, {Fd, M});
    });
}
Value spectralKurtosis(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralKurtosis(x, f, StftOptions{}, mr);
}

Value spectralSkewness(const Value &x, const Value &f,
                       const StftOptions &stft, keel::Allocator *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralSkewness", stft);
    const keel::f64 *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const keel::f64 *col, keel::usize M) {
        return keel::audio::spectral_skewness({col, M}, {Fd, M});
    });
}
Value spectralSkewness(const Value &x, const Value &f, keel::Allocator *mr)
{
    return spectralSkewness(x, f, StftOptions{}, mr);
}

// ── Flux ──────────────────────────────────────────────────────────────
// Spectral flux (Peeters 2004) — per-frame metric of spectrum change.
// flux(t) = (Σ|X_t(k) - X_{t-1}(k)|^p)^(1/p) for k = 1..M.
// With no initialCondition the first frame's flux is 0 (probed R2025b);
// with an M×1 ic column, flux(1) is measured against it; the optional
// second output returns the final spectrum column.
// Probed class contract: (x, fs) time form takes double/single real x;
// the (X, F) form also accepts complex X (|·| inside) while a complex F
// rejects; logical / integer / char reject on both forms; the result
// keeps X's class.
namespace {

void spectralFlux_argcheck(const Value &x, const Value &f, keel::f64 p,
                           const Value *initialCondition, Value *finalCondition)
{
    (void)x;
    if (p != 1.0 && p != 2.0)
        throw Error(keel::format("spectralFlux: NormType must be 1 or 2 (got {}).", p),
                    0, 0, "spectralFlux", "",
                    "numkit:spectralFlux:normType");
    const bool timeForm = f.numel() == 1;
    // f gates first for BOTH forms (the reference's arguments block):
    // nonempty, real, nonnegative, finite.
    if (f.type() == ValueType::COMPLEX)
        throw Error("spectralFlux: input 2 (f) must be real.",
                    0, 0, "spectralFlux", "",
                    "numkit:spectralFlux:mustBeReal");
    for (keel::usize i = 0; i < f.numel(); ++i) {
        const keel::f64 fv = f.elemAsDouble(i);
        if (!(fv >= 0.0))
            throw Error("spectralFlux: input 2 (f) must be nonnegative.",
                        0, 0, "spectralFlux", "",
                        "numkit:spectralFlux:mustBeNonnegative");
        if (!keel::is_finite(fv))
            throw Error("spectralFlux: input 2 (f) must be finite.",
                        0, 0, "spectralFlux", "",
                        "numkit:spectralFlux:mustBeFinite");
    }

    if (timeForm) {
        // Time-domain form: previousSpectrum and a second output are
        // syntax errors (the reference's InvalidFluxSyntax / nargoutchk);
        // the first spectrum of each channel seeds the state, so
        // flux(1) = 0.
        if (initialCondition && !initialCondition->isEmpty())
            throw Error("spectralFlux: the previousSpectrum input is "
                        "only supported for frequency-domain input.",
                        0, 0, "spectralFlux", "",
                        "numkit:spectralFlux:InvalidFluxSyntax");
        if (finalCondition)
            throw Error("spectralFlux: too many output arguments (the "
                        "final spectrum state is only returned for "
                        "frequency-domain input).",
                        0, 0, "spectralFlux", "",
                        "numkit:spectralFlux:nargoutchk");
    }
}

} // anon

Value spectralFluxFull(const Value &x, const Value &f, keel::f64 p,
                       const Value *initialCondition, Value *finalCondition,
                       bool magnitudeSpectrum, const StftOptions &stft,
                       keel::Allocator *mr)
{
    spectralFlux_argcheck(x, f, p, initialCondition, finalCondition);
    const bool timeForm = f.numel() == 1;

    auto d = prepareInput(mr, x, f, magnitudeSpectrum, "spectralFlux", stft);
    const keel::usize M = d.X.dims().rows();
    const keel::usize F = d.numFrames;
    const keel::usize C = d.numChan;

    // Result class follows the INPUT's class (the time form's STFT
    // buffer is double internally; the reference still returns single).
    const bool wantSingle = x.type() == ValueType::SINGLE;
    Value out = Value::matrix(F, C,
                              wantSingle ? ValueType::SINGLE
                                         : ValueType::DOUBLE, mr);
    if (M == 0) return out;
    if (F == 0)
        throw Error("spectralFlux: X must contain at least one frame "
                    "(got a 0-frame spectrum).",
                    0, 0, "spectralFlux", "",
                    "numkit:spectralFlux:emptyFrames");

    // Optional previous-spectrum state (frequency-domain form): one
    // M×1 column per channel.
    const bool haveIc = initialCondition && !initialCondition->isEmpty();
    if (haveIc && !timeForm
        && (initialCondition->numel() != M * C
            || initialCondition->dims().rows() != M))
        throw Error(keel::format("spectralFlux: initialCondition must have one {}-by-1 column per channel (got {} channels).",
                                 M, C),
                    0, 0, "spectralFlux", "",
                    "numkit:spectralFlux:BadInitialConditions");

    const bool cx = d.X.type() == ValueType::COMPLEX;
    keel::f64 *od = wantSingle ? nullptr : out.doubleDataMut();
    keel::f32 *of = wantSingle ? out.singleDataMut() : nullptr;
    const keel::f64 invP = 1.0 / p;
    auto fluxPair = [&](keel::usize curBase, keel::usize prvBase) -> keel::f64 {
        keel::f64 s = 0.0;
        for (keel::usize i = 0; i < M; ++i) {
            const keel::f64 diff = cx
                ? keel::abs(d.X.complexElem(curBase + i)
                           - d.X.complexElem(prvBase + i))
                : keel::abs(d.X.elemAsDouble(curBase + i)
                           - d.X.elemAsDouble(prvBase + i));
            s += keel::pow(diff, p);
        }
        return keel::pow(s, invP);
    };
    auto readSpec = [&](keel::usize base) {
        return cx ? keel::abs(d.X.complexElem(base))
                  : d.X.elemAsDouble(base);
    };
    for (keel::usize c = 0; c < C; ++c) {
        const keel::usize first = (c * F + 0) * M;
        keel::f64 r0 = 0.0;
        if (!timeForm && haveIc) {
            keel::f64 s = 0.0;
            for (keel::usize i = 0; i < M; ++i) {
                const keel::f64 cur = readSpec(first + i);
                const keel::f64 prv = initialCondition->elemAsDouble(c * M + i);
                s += keel::pow(keel::abs(cur - prv), p);
            }
            r0 = keel::pow(s, invP);
        }
        if (of) of[c * F] = keel::as<keel::f32>(r0);
        else od[c * F] = r0;
        for (keel::usize fr = 1; fr < F; ++fr) {
            const keel::f64 r = fluxPair((c * F + fr) * M, (c * F + fr - 1) * M);
            if (of) of[c * F + fr] = keel::as<keel::f32>(r);
            else od[c * F + fr] = r;
        }
    }

    if (finalCondition) {
        // The reference's second output (frequency-domain form only):
        // the final spectrum state, one M×1 column per channel.
        Value fcx = Value::matrix(M, C,
                                  wantSingle ? ValueType::SINGLE
                                             : ValueType::DOUBLE, mr);
        keel::f64 *fd = wantSingle ? nullptr : fcx.doubleDataMut();
        keel::f32 *ff = wantSingle ? fcx.singleDataMut() : nullptr;
        for (keel::usize c = 0; c < C; ++c) {
            const keel::usize last = (c * F + F - 1) * M;
            for (keel::usize i = 0; i < M; ++i) {
                const keel::f64 v = readSpec(last + i);
                if (ff) ff[c * M + i] = keel::as<keel::f32>(v);
                else fd[c * M + i] = v;
            }
        }
        *finalCondition = std::move(fcx);
    }
    return out;
}

} // namespace numkit::audio
