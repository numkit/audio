// toolboxes/audio/src/spectral/melspec_delta.cpp
//
// Audio Cycle C — melSpectrogram + audioDelta.
//
// melSpectrogram — triangular mel filterbank (Davis & Mermelstein,
// IEEE TASSP, 1980). Default parameters:
//   window = hamming(round(0.03*fs), 'periodic')
//   overlap = round(0.02*fs)
//   FFTLength = numel(window)
//   FrequencyRange = [0, fs/2]
//   NumBands = 32
//   MelStyle = 'oshaughnessy' (2595*log10(1+hz/700))
//   Normalization = 'bandwidth' (each filter divided by its bandwidth)
//   SpectrumType = 'power'
//   WindowNormalization = true → win /= sqrt(0.5 * sum(win)^2)
//
// audioDelta — regression delta coefficients (Furui, IEEE TASSP, 1986):
//   M = floor(windowLength/2)
//   b = (M:-1:-M) / sum((1:M).^2)
//   delta = filter(b, 1, x, [], 1)  (causal, along dim 1)
//
// PMR HARD RULE: every fn takes std::pmr::memory_resource *mr.
//
// As of Cycle J the per-frame FFT uses toolboxes/signal::fft (Bluestein
// chirp-z for non-pow2 winLen). The 'periodic' hamming definition
// (cos arg 2π·n/N rather than 2π·n/(N-1)) matches MATLAB exactly.

#include <numkit/audio/spectral/melspec_delta.hpp>

// Compute-only TU: Value substrate + Error, no engine. The melSpectrogram
// / audioDelta builtins (CallContext wrappers) live in
// spectral/melspec_delta_reg.cpp.
#include <numkit/value/value.hpp>
#include <numkit/value/error.hpp>
#include <numkit/value/scratch.hpp>

#include "fft_one_sided.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace numkit::audio {

namespace {

// Periodic Hamming window: w[n] = 0.54 - 0.46 * cos(2π·n / N), n=0..N-1.
// (Symmetric Hamming would use N-1 in the denominator.)
void hammingPeriodic(double *w, size_t N)
{
    for (size_t n = 0; n < N; ++n)
        w[n] = 0.54 - 0.46 * std::cos(2.0 * M_PI * static_cast<double>(n)
                                        / static_cast<double>(N));
}

// Mel <-> Hz (O'Shaughnessy default).
inline double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
inline double melToHz(double m)  { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); }

// Triangular mel filterbank with 'bandwidth' normalization.
// Returns NumBands × NumBins matrix (column-major) and 1 × NumBands center
// frequencies in Hz.
struct MelFB {
    Value FB;        // NumBands × NumBins
    Value F;         // 1 × NumBands center frequencies (Hz)
};

MelFB designMelFilterBank(std::pmr::memory_resource *mr,
                           double fs, int numBands, int fftLen)
{
    const size_t M = static_cast<size_t>(numBands);
    const size_t H = static_cast<size_t>(fftLen / 2 + 1);
    const double melMax = hzToMel(fs * 0.5);

    // Build mel edges: numBands+2 equally spaced from 0 to melMax.
    // Convert to Hz, then to FFT bin (continuous) for triangle vertices.
    Value FB = Value::matrix(M, H, ValueType::DOUBLE, mr);
    Value F  = Value::matrix(1, M, ValueType::DOUBLE, mr);
    if (M == 0 || H == 0 || fftLen <= 0) return {FB, F};

    double *fbd = FB.doubleDataMut();
    std::fill(fbd, fbd + M * H, 0.0);
    double *fd = F.doubleDataMut();

    // Bin frequency for each FFT bin k: fk = k * fs / fftLen.
    // Triangle for band b spans (mel_edge[b-1], mel_edge[b+1]) with peak
    // at mel_edge[b]. Convert edges to Hz, then to bin index.
    for (size_t b = 0; b < M; ++b) {
        const double m_lo = static_cast<double>(b)     / static_cast<double>(M + 1) * melMax;
        const double m_ce = static_cast<double>(b + 1) / static_cast<double>(M + 1) * melMax;
        const double m_hi = static_cast<double>(b + 2) / static_cast<double>(M + 1) * melMax;
        const double f_lo = melToHz(m_lo);
        const double f_ce = melToHz(m_ce);
        const double f_hi = melToHz(m_hi);
        fd[b] = f_ce;

        for (size_t k = 0; k < H; ++k) {
            const double f = static_cast<double>(k) * fs / static_cast<double>(fftLen);
            double val = 0.0;
            if (f >= f_lo && f <= f_ce && f_ce > f_lo)
                val = (f - f_lo) / (f_ce - f_lo);
            else if (f > f_ce && f <= f_hi && f_hi > f_ce)
                val = (f_hi - f) / (f_hi - f_ce);
            fbd[b + k * M] = val;
        }
        // Bandwidth normalization: divide by total bandwidth (Hz).
        const double bw = f_hi - f_lo;
        if (bw > 0.0) {
            const double inv = 2.0 / bw;  // factor matches MATLAB area=2/bw
            for (size_t k = 0; k < H; ++k)
                fbd[b + k * M] *= inv;
        }
    }
    return {FB, F};
}

} // anon

std::tuple<Value, Value, Value>
melSpectrogram(const Value &x, double fs, int numBands,
               std::pmr::memory_resource *mr)
{
    const size_t N = x.numel();
    const size_t winLen  = static_cast<size_t>(std::round(fs * 0.03));
    const size_t overlap = static_cast<size_t>(std::round(fs * 0.02));
    const size_t hop = winLen - overlap;
    const size_t fftLen = winLen;
    const size_t H = fftLen / 2 + 1;
    const size_t numFrames = (N >= winLen) ? ((N - winLen) / hop + 1) : 0;
    const size_t M = static_cast<size_t>(numBands);

    Value S = Value::matrix(M, numFrames, ValueType::DOUBLE, mr);
    Value F = Value::matrix(1, M, ValueType::DOUBLE, mr);
    Value T = Value::matrix(numFrames, numFrames == 0 ? 0 : 1, ValueType::DOUBLE, mr);

    if (numFrames == 0 || winLen == 0 || M == 0) return {S, F, T};

    ScratchArena scratch(mr);
    ScratchVec<double> win(winLen, &scratch);
    hammingPeriodic(win.data(), winLen);

    // WindowNormalization (power form): win /= sqrt(0.5 * sum(win)^2)
    double sumW = 0.0;
    for (size_t i = 0; i < winLen; ++i) sumW += win[i];
    const double scale = std::sqrt(0.5 * sumW * sumW);
    if (scale > 0.0)
        for (size_t i = 0; i < winLen; ++i) win[i] /= scale;

    // Filterbank
    auto fb = designMelFilterBank(mr, fs, numBands, static_cast<int>(fftLen));
    std::copy(fb.F.doubleData(), fb.F.doubleData() + M, F.doubleDataMut());
    const double *fbd = fb.FB.doubleData();

    // Time vector.
    double *td = T.doubleDataMut();
    for (size_t f = 0; f < numFrames; ++f)
        td[f] = (static_cast<double>(f * hop) + static_cast<double>(winLen) / 2.0) / fs;

    // Per-frame: window, FFT, power, mel-filterbank.
    ScratchVec<double> frame(winLen, &scratch);
    ScratchVec<double> pow_half(H, &scratch);
    double *Sd = S.doubleDataMut();
    for (size_t f = 0; f < numFrames; ++f) {
        const size_t start = f * hop;
        for (size_t i = 0; i < winLen; ++i)
            frame[i] = x.elemAsDouble(start + i) * win[i];
        detail::fftPowerHalf(mr, frame.data(), winLen, pow_half.data());
        // Apply filterbank: S(b, f) = Σ FB(b, k) * pow(k)
        for (size_t b = 0; b < M; ++b) {
            double s = 0.0;
            for (size_t k = 0; k < H; ++k) s += fbd[b + k * M] * pow_half[k];
            Sd[b + f * M] = s;
        }
    }
    return {S, F, T};
}

// ── audioDelta ────────────────────────────────────────────────────────
// b[k] = (M - k) / sum((1:M).^2) for k=0..2M (length 2M+1 = windowLength).
// y[n] = Σ_{k=0..2M} b[k] * x[n-k] (causal filter, zero initial state).
// Operates along dim 1 (rows are time); columns AND pages are channels.
//
// Probed R2025b contract: x is double/single real (logical / integer /
// char / complex reject), the result keeps x's class, 3-D input filters
// page-wise, and the optional initialCondition / second output follow
// the MATLAB filter() zi/zf state convention (verified on values):
//   y(n) = b0·x(n) + z1,  z1' = b1·x(n) + z2, …  zf = [z1 … z_{L-1}]
// with zi shaped (windowLength-1) × channels.
Value audioDeltaFull(const Value &x, int windowLength,
                     const Value *initialCondition, Value *finalCondition,
                     std::pmr::memory_resource *mr)
{
    if (windowLength < 3 || (windowLength % 2) == 0)
        throw Error("audioDelta: windowLength must be an odd integer "
                    "greater than 1 (got " + std::to_string(windowLength) + ").",
                    0, 0, "audioDelta", "", "nk:audioDelta:BadWin");

    if (x.type() == ValueType::COMPLEX)
        throw Error("audioDelta: input 1 (x) must be real.",
                    0, 0, "audioDelta", "", "nk:audioDelta:expectedReal");
    if (x.type() != ValueType::DOUBLE && x.type() != ValueType::SINGLE
        && x.type() != ValueType::EMPTY)
        throw Error("audioDelta: input 1 (x) must be double or single "
                    "(got a " + std::string(mtypeName(x.type())) + " array).",
                    0, 0, "audioDelta", "", "nk:audioDelta:invalidType");

    const int M = windowLength / 2;
    // Coefficients [M, M-1, ..., 1, 0, -1, ..., -M] / sum((1:M)^2)
    double denom = 0.0;
    for (int i = 1; i <= M; ++i) denom += static_cast<double>(i * i);
    if (denom <= 0.0) denom = 1.0;
    const size_t L = static_cast<size_t>(windowLength);

    const bool wantSingle = x.type() == ValueType::SINGLE;
    const Dims &d = x.dims();
    const size_t R = d.dim(0);
    // Channels = columns × pages × … (everything after dim 1).
    size_t C = 1;
    for (int k = 1; k < d.ndim(); ++k) C *= d.dim(k);
    Value out;
    if (d.ndim() <= 2)
        out = Value::matrix(R, C, wantSingle ? ValueType::SINGLE
                                             : ValueType::DOUBLE, mr);
    else {
        std::vector<size_t> od(d.ndim());
        od[0] = R;
        for (int k = 1; k < d.ndim(); ++k) od[k] = d.dim(k);
        out = Value::matrixND(od.data(), static_cast<int>(od.size()),
                              wantSingle ? ValueType::SINGLE
                                         : ValueType::DOUBLE, mr);
    }
    if (R == 0 || C == 0) return out;

    // Optional zi state: (L-1) × channels, same column/page layout.
    const bool haveZi = initialCondition && !initialCondition->isEmpty();
    // ic is either the exact (wl-1) × channels state matrix or a single
    // (wl-1)-element state broadcast to every channel (the reference
    // accepts audioDelta(1:10, 3, [1 2]) for a 10-channel row — probed).
    const bool singleState = haveZi && initialCondition->numel() == L - 1;
    if (haveZi) {
        if (!singleState
            && (initialCondition->dims().dim(0) != L - 1
                || initialCondition->numel() != (L - 1) * C))
            throw Error("audioDelta: initialCondition must have "
                        + std::to_string(L - 1) + " rows and "
                        + std::to_string(C) + " channels.",
                        0, 0, "audioDelta", "",
                        "nk:audioDelta:BadInitialConditions");
        if (initialCondition->type() == ValueType::COMPLEX)
            throw Error("audioDelta: initialCondition must be real.",
                        0, 0, "audioDelta", "",
                        "nk:audioDelta:BadInitialConditions");
    }

    ScratchArena scratch(mr);
    ScratchVec<double> b(L, &scratch);
    for (size_t k = 0; k < L; ++k)
        b[k] = static_cast<double>(M - static_cast<int>(k)) / denom;
    // Filter delay line, (L-1) per channel — zero-initialised, or seeded
    // from initialCondition (MATLAB filter zi convention).
    ScratchVec<double> z((L - 1) * C, &scratch);
    for (std::size_t i = 0; i < z.size(); ++i) {
        if (!haveZi)
            z[i] = 0.0;
        else if (singleState)
            z[i] = initialCondition->elemAsDouble(i % (L - 1));
        else
            z[i] = initialCondition->elemAsDouble(i);
    }

    double *od = wantSingle ? nullptr : out.doubleDataMut();
    float *of = wantSingle ? out.singleDataMut() : nullptr;
    for (size_t c = 0; c < C; ++c) {
        double *zc = z.data() + c * (L - 1);
        for (size_t n = 0; n < R; ++n) {
            const double xv = x.elemAsDouble(n + c * R);
            // Direct-form II transposed step: y = b0·x + z1, then shift.
            const double y = b[0] * xv + (L > 1 ? zc[0] : 0.0);
            for (size_t k = 0; k + 2 < L; ++k)
                zc[k] = b[k + 1] * xv + zc[k + 1];
            if (L > 1) zc[L - 2] = b[L - 1] * xv;
            if (of) of[n + c * R] = static_cast<float>(y);
            else    od[n + c * R] = y;
        }
    }

    if (finalCondition) {
        // zf mirrors zi's shape ((L-1) × channels, 2-D).
        *finalCondition = Value::matrix(L - 1, C,
                                        wantSingle ? ValueType::SINGLE
                                                   : ValueType::DOUBLE, mr);
        double *fd = wantSingle ? nullptr : finalCondition->doubleDataMut();
        float *ff = wantSingle ? finalCondition->singleDataMut() : nullptr;
        for (std::size_t i = 0; i < (L - 1) * C; ++i) {
            const double v = z[i];
            if (ff) ff[i] = static_cast<float>(v);
            else    fd[i] = v;
        }
    }
    return out;
}

Value audioDelta(const Value &x, int windowLength, std::pmr::memory_resource *mr)
{
    return audioDeltaFull(x, windowLength, nullptr, nullptr, mr);
}


} // namespace numkit::audio
