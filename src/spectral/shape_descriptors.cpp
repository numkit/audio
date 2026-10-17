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

#include <numkit/audio/spectral/shape_descriptors.hpp>

// Compute-only TU: Value substrate + Error, no engine. The spectral*
// builtins (CallContext wrappers) live in spectral/shape_descriptors_reg.cpp.
#include <numkit/value/value.hpp>
#include <numkit/value/error.hpp>
#include <numkit/value/scratch.hpp>

#include "fft_one_sided.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace numkit::audio {

namespace {

struct Stft {
    Value X;              // M × (F·C), channel-major (channel c's frames
                          // occupy columns [c·F, c·F+F)); M = selected bins
    Value F;              // M × 1, frequency axis in Hz
    size_t numChan = 1;
    size_t numFrames = 0;
};

// Shared argument validation for the spectral-descriptor options, in the
// reference's check order (window first, then overlap, FFT length,
// range) — the blame follows the first failing argument.
static void requireFloatClass(const Value &v, const char *fn,
                              const char *argName, int argNum)
{
    if (v.type() == ValueType::SINGLE || v.type() == ValueType::DOUBLE
        || v.type() == ValueType::COMPLEX || v.type() == ValueType::EMPTY)
        return;
    throw Error(std::string(fn) + ": input " + std::to_string(argNum)
                    + " (" + argName + ") must be single or double, not "
                    + mtypeName(v.type()) + ".",
                0, 0, fn, "", std::string("numkit:") + fn + ":invalidType");
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
static Stft computeStft(std::pmr::memory_resource *mr, const Value &x,
                        double fs, bool magnitude, const char *fn,
                        const StftOptions &opts)
{
    const size_t xLen   = x.dims().rows();
    const size_t numChan = x.dims().cols();
    const bool haveWin = !opts.Window.isEmpty();
    const bool haveOvl = !opts.OverlapLength.isEmpty();

    // ── window ──
    std::vector<double> win;
    size_t winLen;
    if (!haveWin) {
        const double wNatural = std::round(fs * 0.03);
        if (wNatural > 120.0) {
            if (wNatural > static_cast<double>(xLen))
                throw Error(std::string(fn) + ": the default window "
                                "length round(0.03·f) = "
                                + std::to_string(static_cast<long long>(
                                      wNatural))
                                + " exceeds the signal length "
                                + std::to_string(xLen) + ".",
                            0, 0, fn, "",
                            std::string("numkit:") + fn + ":BadWindowLength");
            winLen = static_cast<size_t>(wNatural);
            win.assign(winLen, 1.0);
        } else if (!haveOvl) {
            winLen = std::min<size_t>(64, xLen);
            win.assign(winLen, 1.0);
        } else {
            // "8 segments" geometry: window sized so that 8 hops of
            // (window − ovl) tile the signal exactly.
            const double ov0 = opts.OverlapLength.elemAsDouble(0);
            winLen = static_cast<size_t>(
                std::floor((static_cast<double>(xLen) + 7.0 * ov0) / 8.0));
            win.assign(winLen, 1.0);
        }
    } else {
        const Value &W = opts.Window;
        requireFloatClass(W, fn, "Window", 3);
        if (W.type() == ValueType::COMPLEX)
            throw Error(std::string(fn) + ": input Window must be real.",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedReal");
        if (W.numel() == 0)
            throw Error(std::string(fn) + ": input Window must be "
                        "nonempty.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedNonempty");
        if (W.dims().cols() != 1 && W.numel() > 1)
            throw Error(std::string(fn) + ": input Window must be a "
                        "column vector.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedColumn");
        const size_t wn = W.numel();
        for (size_t i = 0; i < wn; ++i)
            if (!std::isfinite(W.elemAsDouble(i)))
                throw Error(std::string(fn) + ": input Window must be "
                            "finite.", 0, 0, fn, "",
                            std::string("numkit:") + fn + ":expectedFinite");
        if (wn <= 2 || wn > xLen)
            throw Error(std::string(fn) + ": numel(Window) must be "
                            "greater than 2 and at most the signal "
                            "length " + std::to_string(xLen) + " (got "
                            + std::to_string(wn) + ").",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":notLessEqual");
        winLen = wn;
        win.resize(wn);
        for (size_t i = 0; i < wn; ++i) win[i] = W.elemAsDouble(i);
    }

    // ── overlap ──
    size_t overlap;
    if (!haveOvl) {
        const double oNat = std::round(fs * 0.02);
        overlap = (oNat > 80.0) ? static_cast<size_t>(oNat)
                                : winLen / 2;      // fix(0.5·windowLength)
    } else {
        const Value &O = opts.OverlapLength;
        requireFloatClass(O, fn, "OverlapLength", 3);
        if (O.type() == ValueType::COMPLEX)
            throw Error(std::string(fn) + ": input OverlapLength must be "
                        "real.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedReal");
        if (O.numel() != 1)
            throw Error(std::string(fn) + ": input OverlapLength must be "
                        "a scalar.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedScalar");
        const double v = O.elemAsDouble(0);
        if (!(v == std::floor(v)))
            throw Error(std::string(fn) + ": input OverlapLength must be "
                        "an integer.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedInteger");
        if (v < 0.0)
            throw Error(std::string(fn) + ": input OverlapLength must be "
                        "nonnegative.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedNonnegative");
        if (v >= static_cast<double>(winLen))
            throw Error(std::string(fn) + ": OverlapLength must be "
                            "smaller than the window length "
                            + std::to_string(winLen) + ".",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":BadOverlapLength");
        overlap = static_cast<size_t>(v);
    }

    // ── FFT length ──
    size_t nfft = winLen;
    if (!opts.FFTLength.isEmpty()) {
        const Value &Nv = opts.FFTLength;
        if (Nv.type() == ValueType::COMPLEX)
            throw Error(std::string(fn) + ": input FFTLength must be "
                        "real.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedReal");
        if (Nv.numel() != 1)
            throw Error(std::string(fn) + ": input FFTLength must be a "
                        "scalar.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedScalar");
        const double v = Nv.elemAsDouble(0);
        if (!(v == std::floor(v)) || v <= 0.0)
            throw Error(std::string(fn) + ": input FFTLength must be a "
                        "positive integer.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":mustBePositive");
        if (v < 2.0)
            throw Error(std::string(fn) + ": FFTLength must be at least "
                        "2.", 0, 0, fn, "",
                        std::string("numkit:") + fn
                            + ":mustBeGreaterThanOrEqual");
        nfft = static_cast<size_t>(v);
    }
    if (nfft < winLen)
        throw Error(std::string(fn) + ": the FFT length must be greater "
                        "than or equal to the window length, "
                        + std::to_string(winLen) + ".",
                    0, 0, fn, "",
                    std::string("numkit:") + fn
                        + ":FFTLengthLessThanWindowLength");

    // ── range ──
    double rLo = 0.0, rHi;
    if (fs > 1.0) rHi = std::floor(fs / 2.0); else rHi = fs / 2.0;
    if (!opts.Range.isEmpty()) {
        const Value &R = opts.Range;
        requireFloatClass(R, fn, "Range", 3);
        if (R.type() == ValueType::COMPLEX)
            throw Error(std::string(fn) + ": input Range must be real.",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedReal");
        if (R.numel() != 2 || R.dims().rows() != 1)
            throw Error(std::string(fn) + ": input Range must be a 1-by-2 "
                        "row vector.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedRow");
        rLo = R.elemAsDouble(0);
        rHi = R.elemAsDouble(1);
        if (!std::isfinite(rLo) || !std::isfinite(rHi))
            throw Error(std::string(fn) + ": input Range must be finite.",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedFinite");
        if (!(rLo < rHi))
            throw Error(std::string(fn) + ": Expected Range to be "
                        "strictly increasing.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedIncreasing");
        if (rLo < 0.0)
            throw Error(std::string(fn) + ": input Range must be "
                        "nonnegative.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedNonnegative");
        if (rHi > fs / 2.0)
            throw Error(std::string(fn) + ": Range(2) must be at most "
                        "fs/2 = " + std::to_string(fs / 2.0) + ".",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":notLessEqual");
    }

    const size_t hop = winLen - overlap;
    const size_t numFrames =
        (xLen >= winLen) ? ((xLen - winLen) / hop + 1) : 0;

    // Selected bins (1-based in the reference; 0-based here after −1).
    const size_t binLow  = static_cast<size_t>(
        std::ceil(rLo * static_cast<double>(nfft) / fs + 1.0));
    const size_t binHigh = static_cast<size_t>(
        std::floor(rHi * static_cast<double>(nfft) / fs + 1.0));
    const size_t M = (binHigh >= binLow) ? (binHigh - binLow + 1) : 0;

    Stft s;
    s.numChan = numChan;
    s.numFrames = numFrames;
    s.X = Value::matrix(M, numFrames * numChan, ValueType::DOUBLE, mr);
    s.F = Value::matrix(M, 1, ValueType::DOUBLE, mr);
    if (numFrames == 0 || M == 0) return s;

    double *fd = s.F.doubleDataMut();
    for (size_t k = 0; k < M; ++k)
        fd[k] = (fs / static_cast<double>(nfft))
                * static_cast<double>(binLow - 1 + k);
    if ((nfft % 2) != 0 && binHigh == nfft / 2 + 1 && M > 0)
        fd[M - 1] = fs * static_cast<double>(nfft - 1)
                    / (2.0 * static_cast<double>(nfft));

    double winSum = 0.0;
    for (double w : win) winSum += w;
    const double invPow = 1.0 / (0.5 * winSum * winSum);
    const double magScl = 2.0 / winSum;
    const bool halveDc = (binLow == 1);
    const bool halveNyq = (binHigh == nfft / 2 + 1) && (nfft % 2 == 0);

    ScratchArena scratch(mr);
    ScratchVec<double> frame(winLen, &scratch);
    ScratchVec<double> half(nfft / 2 + 1, &scratch);
    double *Xd = s.X.doubleDataMut();
    for (size_t c = 0; c < numChan; ++c) {
        for (size_t f = 0; f < numFrames; ++f) {
            const size_t start = f * hop;
            for (size_t i = 0; i < winLen; ++i)
                frame[i] = x.elemAsDouble(c * xLen + start + i) * win[i];
            detail::fftPowerHalfN(mr, frame.data(), winLen, nfft,
                                  half.data());
            double *col = Xd + (c * numFrames + f) * M;
            for (size_t k = 0; k < M; ++k) {
                const double p = half[binLow - 1 + k];
                col[k] = magnitude ? magScl * std::sqrt(p) : p * invPow;
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
    size_t numChan = 1;
    size_t numFrames = 0;
};

InputData prepareInput(std::pmr::memory_resource *mr,
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
            throw Error(std::string(fn) + ": input 1 (x) must be real.",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedReal");
        if (x.isEmpty())
            throw Error(std::string(fn) + ": input 1 (x) must be "
                        "nonempty.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expectedNonempty");
        if (x.dims().pages() > 1)
            throw Error(std::string(fn) + ": Expected x to be "
                        "two-dimensional.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":expected2D");
        if (x.dims().rows() < 2)
            throw Error(std::string(fn) + ": Expected the length of the "
                        "signals in \"x\" to be larger than 1.",
                        0, 0, fn, "",
                        std::string("numkit:") + fn
                            + ":insufficientSignalLength");
        for (size_t i = 0; i < x.numel(); ++i)
            if (!std::isfinite(x.elemAsDouble(i)))
                throw Error(std::string(fn) + ": input 1 (x) must be "
                            "finite.", 0, 0, fn, "",
                            std::string("numkit:") + fn + ":expectedFinite");
        const double fs = f.elemAsDouble(0);
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
            throw Error(std::string(fn) + ": Expected the number of rows "
                        "of \"x\" to be larger than 1.", 0, 0, fn, "",
                        std::string("numkit:") + fn + ":insufficientSignalLength");
        if (f.numel() != x.dims().rows())
            throw Error(std::string(fn) + ": size(x,1) must equal "
                            "numel(f) (got " + std::to_string(x.dims().rows())
                            + " and " + std::to_string(f.numel()) + ").",
                        0, 0, fn, "",
                        std::string("numkit:") + fn + ":BadFrequencyVector");
        d.X = x;
        d.F = f;
        d.isTime = false;
        d.numChan = std::max<size_t>(1, x.dims().pages());
        d.numFrames = x.dims().cols();
    }
    return d;
}

// Iterate (frame, channel) columns; emit the metric matrix F×C. Both
// layouts share the arithmetic: column (m, c) starts at (c·F + m)·M —
// the time form's STFT matrix is channel-major by construction, and a
// frequency-domain L×M×C array is column-major already.
template <typename Cb>
Value perFrameChannel(std::pmr::memory_resource *mr, const Value &X,
                      const InputData &d, Cb cb)
{
    const size_t M = X.dims().rows();
    const size_t F = d.numFrames;
    const size_t C = d.numChan;
    Value out = Value::matrix(F, C,
                              d.wantSingle ? ValueType::SINGLE
                                           : ValueType::DOUBLE,
                              mr);
    if (M == 0) return out;
    double *od = d.wantSingle ? nullptr : out.doubleDataMut();
    float *of = d.wantSingle ? out.singleDataMut() : nullptr;
    ScratchArena scratch(mr);
    ScratchVec<double> col(M, &scratch);
    for (size_t c = 0; c < C; ++c) {
        for (size_t m = 0; m < F; ++m) {
            const size_t base = (c * F + m) * M;
            for (size_t i = 0; i < M; ++i) col[i] = X.elemAsDouble(base + i);
            const double v = cb(col.data(), M);
            if (of) of[m + c * F] = static_cast<float>(v);
            else od[m + c * F] = v;
        }
    }
    return out;
}

double centroidOf(const double *X, const double *F, size_t M)
{
    double sumX = 0.0, sumFX = 0.0;
    for (size_t i = 0; i < M; ++i) { sumX += X[i]; sumFX += F[i] * X[i]; }
    return (sumX > 0.0) ? sumFX / sumX : 0.0;
}

} // anon

Value spectralFlux(const Value &x, const Value &f, double p,
                   std::pmr::memory_resource *mr)
{
    return spectralFluxFull(x, f, p, nullptr, nullptr, false, {}, mr);
}

// ── Centroid ──────────────────────────────────────────────────────────
Value spectralCentroid(const Value &x, const Value &f,
                       const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralCentroid", stft);
    const double *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const double *col, size_t M) {
        return centroidOf(col, Fd, M);
    });
}
Value spectralCentroid(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralCentroid(x, f, StftOptions{}, mr);
}

// ── Spread ────────────────────────────────────────────────────────────
Value spectralSpread(const Value &x, const Value &f,
                     const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralSpread", stft);
    const double *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const double *col, size_t M) {
        const double c = centroidOf(col, Fd, M);
        double sumX = 0.0, sumDX = 0.0;
        for (size_t i = 0; i < M; ++i) {
            const double dF = Fd[i] - c;
            sumX  += col[i];
            sumDX += dF * dF * col[i];
        }
        return (sumX > 0.0) ? std::sqrt(sumDX / sumX) : 0.0;
    });
}
Value spectralSpread(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralSpread(x, f, StftOptions{}, mr);
}

// ── Rolloff point ─────────────────────────────────────────────────────
Value spectralRolloffPoint(const Value &x, const Value &f, double percentile,
                           const StftOptions &stft,
                           std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralRolloffPoint", stft);
    const double *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd, percentile](const double *col, size_t M) {
        double total = 0.0;
        for (size_t i = 0; i < M; ++i) total += col[i];
        const double thresh = percentile * total;
        if (total <= 0.0 || M == 0) return 0.0;
        double accum = 0.0;
        for (size_t i = 0; i < M; ++i) {
            accum += col[i];
            if (accum >= thresh) return Fd[i];
        }
        return Fd[M - 1];
    });
}
Value spectralRolloffPoint(const Value &x, const Value &f, double percentile,
                           std::pmr::memory_resource *mr)
{
    return spectralRolloffPoint(x, f, percentile, StftOptions{}, mr);
}

// ── Decrease ──────────────────────────────────────────────────────────
Value spectralDecrease(const Value &x, const Value &f,
                       const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralDecrease", stft);
    return perFrameChannel(mr, d.X, d, [](const double *col, size_t M) {
        if (M < 2) return 0.0;
        const double X1 = col[0];
        double num = 0.0, den = 0.0;
        for (size_t k = 1; k < M; ++k) {
            num += (col[k] - X1) / static_cast<double>(k);
            den += col[k];
        }
        return (den > 0.0) ? num / den : 0.0;
    });
}
Value spectralDecrease(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralDecrease(x, f, StftOptions{}, mr);
}

// ── Slope ─────────────────────────────────────────────────────────────
Value spectralSlope(const Value &x, const Value &f,
                    const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralSlope", stft);
    const double *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const double *col, size_t M) {
        if (M < 2) return 0.0;
        double meanF = 0.0, meanX = 0.0;
        for (size_t i = 0; i < M; ++i) { meanF += Fd[i]; meanX += col[i]; }
        meanF /= static_cast<double>(M);
        meanX /= static_cast<double>(M);
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < M; ++i) {
            const double df = Fd[i] - meanF;
            num += df * (col[i] - meanX);
            den += df * df;
        }
        return (den > 0.0) ? num / den : 0.0;
    });
}
Value spectralSlope(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralSlope(x, f, StftOptions{}, mr);
}

// ── Cycle I: Crest / Entropy / Flatness / Kurtosis / Skewness ─────────

Value spectralCrest(const Value &x, const Value &f,
                    const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralCrest", stft);
    return perFrameChannel(mr, d.X, d, [](const double *col, size_t M) {
        if (M == 0) return 0.0;
        double mx = col[0], sm = 0.0;
        for (size_t i = 0; i < M; ++i) { if (col[i] > mx) mx = col[i]; sm += col[i]; }
        const double mean = sm / static_cast<double>(M);
        return (mean > 0.0) ? mx / mean : 0.0;
    });
}
Value spectralCrest(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralCrest(x, f, StftOptions{}, mr);
}

Value spectralEntropy(const Value &x, const Value &f,
                      const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralEntropy", stft);
    return perFrameChannel(mr, d.X, d, [](const double *col, size_t M) {
        if (M < 2) return 0.0;
        double sm = 0.0;
        for (size_t i = 0; i < M; ++i) sm += col[i];
        if (sm <= 0.0) return 0.0;
        double H = 0.0;
        const double inv_log2 = 1.0 / std::log(2.0);
        for (size_t i = 0; i < M; ++i) {
            const double q = col[i] / sm;
            if (q > 0.0) H -= q * std::log(q) * inv_log2;
        }
        return H / (std::log(static_cast<double>(M)) * inv_log2);
    });
}
Value spectralEntropy(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralEntropy(x, f, StftOptions{}, mr);
}

Value spectralFlatness(const Value &x, const Value &f,
                       const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralFlatness", stft);
    return perFrameChannel(mr, d.X, d, [](const double *col, size_t M) {
        if (M == 0) return 0.0;
        const double eps = std::numeric_limits<double>::epsilon();
        double sumLog = 0.0, sumX = 0.0;
        for (size_t i = 0; i < M; ++i) {
            sumLog += std::log(col[i] + eps);
            sumX   += col[i];
        }
        const double geom  = std::exp(sumLog / static_cast<double>(M));
        const double arith = sumX / static_cast<double>(M);
        return (arith > 0.0) ? geom / arith : 0.0;
    });
}
Value spectralFlatness(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralFlatness(x, f, StftOptions{}, mr);
}

Value spectralKurtosis(const Value &x, const Value &f,
                       const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralKurtosis", stft);
    const double *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const double *col, size_t M) {
        if (M < 2) return 0.0;
        double sumX = 0.0, sumFX = 0.0;
        for (size_t i = 0; i < M; ++i) { sumX += col[i]; sumFX += Fd[i] * col[i]; }
        if (sumX <= 0.0) return 0.0;
        const double centroid = sumFX / sumX;
        double m2 = 0.0, m4 = 0.0;
        for (size_t i = 0; i < M; ++i) {
            const double dF = Fd[i] - centroid;
            const double dF2 = dF * dF;
            m2 += dF2 * col[i];
            m4 += dF2 * dF2 * col[i];
        }
        const double spread2 = m2 / sumX;
        if (spread2 <= 0.0) return 0.0;
        const double spread4 = spread2 * spread2;
        return m4 / (spread4 * sumX);
    });
}
Value spectralKurtosis(const Value &x, const Value &f, std::pmr::memory_resource *mr)
{
    return spectralKurtosis(x, f, StftOptions{}, mr);
}

Value spectralSkewness(const Value &x, const Value &f,
                       const StftOptions &stft, std::pmr::memory_resource *mr)
{
    auto d = prepareInput(mr, x, f, false, "spectralSkewness", stft);
    const double *Fd = d.F.doubleData();
    return perFrameChannel(mr, d.X, d, [Fd](const double *col, size_t M) {
        if (M < 2) return 0.0;
        double sumX = 0.0, sumFX = 0.0;
        for (size_t i = 0; i < M; ++i) { sumX += col[i]; sumFX += Fd[i] * col[i]; }
        if (sumX <= 0.0) return 0.0;
        const double centroid = sumFX / sumX;
        double m2 = 0.0, m3 = 0.0;
        for (size_t i = 0; i < M; ++i) {
            const double dF = Fd[i] - centroid;
            m2 += dF * dF * col[i];
            m3 += dF * dF * dF * col[i];
        }
        const double spread2 = m2 / sumX;
        if (spread2 <= 0.0) return 0.0;
        const double spread3 = spread2 * std::sqrt(spread2);
        return m3 / (spread3 * sumX);
    });
}
Value spectralSkewness(const Value &x, const Value &f, std::pmr::memory_resource *mr)
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
Value spectralFluxFull(const Value &x, const Value &f, double p,
                       const Value *initialCondition, Value *finalCondition,
                       bool magnitudeSpectrum, const StftOptions &stft,
                       std::pmr::memory_resource *mr)
{
    const bool timeForm = f.numel() == 1;
    // f gates first for BOTH forms (the reference's arguments block):
    // nonempty, real, nonnegative, finite.
    if (f.type() == ValueType::COMPLEX)
        throw Error("spectralFlux: input 2 (f) must be real.",
                    0, 0, "spectralFlux", "",
                    "numkit:spectralFlux:mustBeReal");
    for (size_t i = 0; i < f.numel(); ++i) {
        const double fv = f.elemAsDouble(i);
        if (!(fv >= 0.0))
            throw Error("spectralFlux: input 2 (f) must be nonnegative.",
                        0, 0, "spectralFlux", "",
                        "numkit:spectralFlux:mustBeNonnegative");
        if (!std::isfinite(fv))
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

    auto d = prepareInput(mr, x, f, magnitudeSpectrum, "spectralFlux", stft);
    const size_t M = d.X.dims().rows();
    const size_t F = d.numFrames;
    const size_t C = d.numChan;

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
        throw Error("spectralFlux: initialCondition must have one "
                        + std::to_string(M) + "-by-1 column per channel "
                        + "(got " + std::to_string(C) + " channels).",
                    0, 0, "spectralFlux", "",
                    "numkit:spectralFlux:BadInitialConditions");

    const bool cx = d.X.type() == ValueType::COMPLEX;
    double *od = wantSingle ? nullptr : out.doubleDataMut();
    float *of = wantSingle ? out.singleDataMut() : nullptr;
    const double invP = 1.0 / p;
    auto fluxPair = [&](size_t curBase, size_t prvBase) -> double {
        double s = 0.0;
        for (size_t i = 0; i < M; ++i) {
            const double diff = cx
                ? std::abs(d.X.complexElem(curBase + i)
                           - d.X.complexElem(prvBase + i))
                : std::abs(d.X.elemAsDouble(curBase + i)
                           - d.X.elemAsDouble(prvBase + i));
            s += std::pow(diff, p);
        }
        return std::pow(s, invP);
    };
    auto readSpec = [&](size_t base) {
        return cx ? std::abs(d.X.complexElem(base))
                  : d.X.elemAsDouble(base);
    };
    for (size_t c = 0; c < C; ++c) {
        const size_t first = (c * F + 0) * M;
        double r0 = 0.0;
        if (!timeForm && haveIc) {
            double s = 0.0;
            for (size_t i = 0; i < M; ++i) {
                const double cur = readSpec(first + i);
                const double prv = initialCondition->elemAsDouble(c * M + i);
                s += std::pow(std::abs(cur - prv), p);
            }
            r0 = std::pow(s, invP);
        }
        if (of) of[c * F] = static_cast<float>(r0);
        else od[c * F] = r0;
        for (size_t fr = 1; fr < F; ++fr) {
            const double r = fluxPair((c * F + fr) * M, (c * F + fr - 1) * M);
            if (of) of[c * F + fr] = static_cast<float>(r);
            else od[c * F + fr] = r;
        }
    }

    if (finalCondition) {
        // The reference's second output (frequency-domain form only):
        // the final spectrum state, one M×1 column per channel.
        Value fcx = Value::matrix(M, C,
                                  wantSingle ? ValueType::SINGLE
                                             : ValueType::DOUBLE, mr);
        double *fd = wantSingle ? nullptr : fcx.doubleDataMut();
        float *ff = wantSingle ? fcx.singleDataMut() : nullptr;
        for (size_t c = 0; c < C; ++c) {
            const size_t last = (c * F + F - 1) * M;
            for (size_t i = 0; i < M; ++i) {
                const double v = readSpec(last + i);
                if (ff) ff[c * M + i] = static_cast<float>(v);
                else fd[c * M + i] = v;
            }
        }
        *finalCondition = std::move(fcx);
    }
    return out;
}

} // namespace numkit::audio
