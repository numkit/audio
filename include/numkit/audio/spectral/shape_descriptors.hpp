// toolboxes/audio/include/nk/audio/spectral/shape_descriptors.hpp
//
// Audio spectral shape descriptors.

#pragma once

#include <memory_resource>
#include <numkit/value/value.hpp>

namespace numkit::audio {

/// @addtogroup group_audio
/// @{


/// @file
/// @ingroup group_audio
/// @brief Spectral-shape descriptors.
///
/// **Two input forms** (shared by every function in this header):
/// 1. `(x, fs)` — `x` is a column-vector time-domain signal, `fs` is
///    the sample rate. Internally computes STFT with
///    `rectwin(round(fs · 0.03))`, overlap `round(fs · 0.02)`,
///    `FFTLength = winLen`. Returns a per-frame column vector.
/// 2. `(X, F)` — `X` is a power (or magnitude) spectrum, `F` is the
///    frequency vector. `X` may be `M × N` (one spectrum per column).
///    Returns a per-column metric as a row vector.
///
/// Throughout this header `x` / `f` follow that convention; only
/// descriptor-specific extra arguments (`percentile`, `p`) are called
/// out separately.

/// Name-value options for the shared internal STFT of the time-domain
/// form (the spectral* Window / OverlapLength / FFTLength / Range
/// surface). An empty Value means "not given" — the default geometry
/// (rectwin window by `round(0.03·fs)` rule, overlap by
/// `round(0.02·fs)` / half-window, FFTLength = window length,
/// Range = [0 floor(fs/2)]) applies.
struct StftOptions {
    Value Window;         ///< column vector; empty → default rectwin
    Value OverlapLength;  ///< scalar; empty → default overlap
    Value FFTLength;      ///< scalar; empty → window length
    Value Range;          ///< 1×2 row [lo hi] in Hz; empty → [0 fs/2]
};

/// @brief Spectral centroid — power-weighted mean frequency.
Value spectralCentroid(const Value &x, const Value &f,
                       std::pmr::memory_resource *mr = nullptr);
Value spectralCentroid(const Value &x, const Value &f,
                       const StftOptions &stft,
                       std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral spread — power-weighted std around the centroid.
Value spectralSpread(const Value &x, const Value &f,
                     std::pmr::memory_resource *mr = nullptr);
Value spectralSpread(const Value &x, const Value &f,
                     const StftOptions &stft,
                     std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral rolloff frequency.
Value spectralRolloffPoint(const Value &x, const Value &f,
                           double percentile = 0.95,
                           std::pmr::memory_resource *mr = nullptr);
Value spectralRolloffPoint(const Value &x, const Value &f,
                           double percentile,
                           const StftOptions &stft,
                           std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral decrease — slope-like measure relative to first bin.
Value spectralDecrease(const Value &x, const Value &f,
                       std::pmr::memory_resource *mr = nullptr);
Value spectralDecrease(const Value &x, const Value &f,
                       const StftOptions &stft,
                       std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral slope — least-squares slope of `(f, X)`.
Value spectralSlope(const Value &x, const Value &f,
                    std::pmr::memory_resource *mr = nullptr);
Value spectralSlope(const Value &x, const Value &f,
                    const StftOptions &stft,
                    std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral flux — `p`-norm of frame-to-frame spectrum differences.
Value spectralFlux(const Value &x, const Value &f, double p = 2.0,
                   std::pmr::memory_resource *mr = nullptr);

/// Full form: NormType p, optional previous-spectrum state in / final
/// spectrum state out (M×1 columns, frequency-domain form only), the
/// SpectrumType selector ('power' default, 'magnitude' = sqrt-scaled
/// spectrum), and the internal-STFT name-value options.
Value spectralFluxFull(const Value &x, const Value &f, double p,
                       const Value *initialCondition, Value *finalCondition,
                       bool magnitudeSpectrum,
                       const StftOptions &stft = {},
                       std::pmr::memory_resource *mr = nullptr);

// ─────────────────────────────────────────────────────────────────────
// Shape statistics. Frequency moments (kurtosis / skewness) use the
// standard X-weighted central-moment definition.
// ─────────────────────────────────────────────────────────────────────

/// @brief Spectral crest — `max(X) / mean(X)` per frame.
Value spectralCrest(const Value &x, const Value &f,
                    std::pmr::memory_resource *mr = nullptr);
Value spectralCrest(const Value &x, const Value &f,
                    const StftOptions &stft,
                    std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral entropy — Shannon entropy of normalised spectrum.
Value spectralEntropy(const Value &x, const Value &f,
                      std::pmr::memory_resource *mr = nullptr);
Value spectralEntropy(const Value &x, const Value &f,
                      const StftOptions &stft,
                      std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral flatness — `geomean(X) / mean(X)`.
Value spectralFlatness(const Value &x, const Value &f,
                       std::pmr::memory_resource *mr = nullptr);
Value spectralFlatness(const Value &x, const Value &f,
                       const StftOptions &stft,
                       std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral kurtosis — X-weighted 4th central moment.
Value spectralKurtosis(const Value &x, const Value &f,
                       std::pmr::memory_resource *mr = nullptr);
Value spectralKurtosis(const Value &x, const Value &f,
                       const StftOptions &stft,
                       std::pmr::memory_resource *mr = nullptr);

/// @brief Spectral skewness — X-weighted 3rd central moment.
Value spectralSkewness(const Value &x, const Value &f,
                       std::pmr::memory_resource *mr = nullptr);
Value spectralSkewness(const Value &x, const Value &f,
                       const StftOptions &stft,
                       std::pmr::memory_resource *mr = nullptr);


/// @}
} // namespace numkit::audio
