// toolboxes/audio/src/spectral/shape_descriptors_reg.cpp
//
// Register half of the spectral-shape descriptors: the CallContext builtins
// spectralCentroid / Spread / RolloffPoint / Decrease / Slope / Crest /
// Entropy / Flatness / Kurtosis / Skewness / Flux that delegate to the
// engine-free compute in shape_descriptors.cpp. library.cpp forward-declares
// + registers these by name.
//
// Compute reconciled to the canonical mr-last API (matching the header) as
// part of this split. Phase 2b — see project_layering_refactor memory.

#include <numkit/audio/spectral/shape_descriptors.hpp>

#include <numkit/sys/plugin.hpp>   // CallContext, ctx.engine->resource()
#include <numkit/value/error.hpp>

#include <string>
#include <utility>

namespace numkit::audio {
namespace detail {

// Per-fn registration adapters share the (x, f) argument shape.
#define NUMKIT_SPEC_REG(FN)                                                          \
    void FN##_reg(Span<const Value> args, size_t /*nargout*/,                    \
                  Span<Value> outs, CallContext &ctx)                            \
    {                                                                            \
        if (args.size() < 2)                                                     \
            throw Error(#FN ": requires (x, fs) or (X, F)",                      \
                        0, 0, #FN, "", "numkit:" #FN ":nargin");                      \
        outs[0] = FN(args[0], args[1], ctx.engine->resource());                  \
    }

NUMKIT_SPEC_REG(spectralCentroid)
NUMKIT_SPEC_REG(spectralSpread)
NUMKIT_SPEC_REG(spectralDecrease)
NUMKIT_SPEC_REG(spectralSlope)
NUMKIT_SPEC_REG(spectralCrest)
NUMKIT_SPEC_REG(spectralEntropy)
NUMKIT_SPEC_REG(spectralFlatness)
NUMKIT_SPEC_REG(spectralKurtosis)
NUMKIT_SPEC_REG(spectralSkewness)

#undef NUMKIT_SPEC_REG

void spectralRolloffPoint_reg(Span<const Value> args, size_t /*nargout*/,
                              Span<Value> outs, CallContext &ctx)
{
    if (args.size() < 2)
        throw Error("spectralRolloffPoint: requires (x, fs [, threshold])",
                    0, 0, "spectralRolloffPoint", "",
                    "numkit:spectralRolloffPoint:nargin");
    double pct = 0.95;
    if (args.size() >= 3) pct = args[2].toScalar();
    outs[0] = spectralRolloffPoint(args[0], args[1], pct,
                                    ctx.engine->resource());
}

void spectralFlux_reg(Span<const Value> args, size_t nargout,
                      Span<Value> outs, CallContext &ctx)
{
    // Probed R2025b signature: spectralFlux(x, f[, initialCondition]
    // [, Name=Value]) with NormType ∈ {1, 2} (default 2), SpectrumType
    // ∈ {'power','magnitude'} (validatestring — unique prefixes match),
    // and the four STFT-reshaping options Window / OverlapLength /
    // FFTLength / Range (validated inside the shared computeStft port).
    if (args.size() < 2)
        throw Error("spectralFlux: requires (x, f[, ic][, Name=Value])",
                    0, 0, "spectralFlux", "", "numkit:spectralFlux:nargin");
    auto *mr = ctx.engine->resource();
    const Value &x = args[0];
    const Value &f = args[1];

    const Value *ic = nullptr;
    double p = 2.0;
    bool magnitude = false;
    StftOptions stft;
    for (std::size_t i = 2; i < args.size();) {
        if (args[i].isChar() || args[i].isString()) {
            std::string name = args[i].toString();
            for (auto &c : name)
                c = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c)));
            if (name == "normtype" || name == "norm") {
                if (i + 1 >= args.size())
                    throw Error("spectralFlux: NormType needs a value.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:nargin");
                const Value &v = args[i + 1];
                if (v.type() == ValueType::COMPLEX)
                    throw Error("spectralFlux: NormType must be real.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:mustBeReal");
                const double pv = v.elemAsDouble(0);
                if (pv != 1.0 && pv != 2.0)
                    throw Error("spectralFlux: NormType must be 1 or 2 "
                                "(got " + std::to_string(pv) + ").",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:normType");
                p = pv;
                i += 2;
            } else if (name == "spectrumtype" || name == "spect") {
                if (i + 1 >= args.size())
                    throw Error("spectralFlux: SpectrumType needs a value.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:nargin");
                std::string val = args[i + 1].toString();
                for (auto &c : val)
                    c = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(c)));
                // validatestring: the value may be any unambiguous
                // case-insensitive prefix of a valid name.
                const bool isPow = std::string("power").rfind(val, 0) == 0;
                const bool isMag = std::string("magnitude").rfind(val, 0) == 0;
                if (isPow == isMag)
                    throw Error("spectralFlux: SpectrumType must be "
                                "'power' or 'magnitude' (got '" + val + "').",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:spectrumType");
                magnitude = isMag;
                i += 2;
            } else if (name == "window") {
                if (i + 1 >= args.size())
                    throw Error("spectralFlux: Window needs a value.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:nargin");
                stft.Window = args[i + 1];
                i += 2;
            } else if (name == "overlaplength") {
                if (i + 1 >= args.size())
                    throw Error("spectralFlux: OverlapLength needs a value.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:nargin");
                stft.OverlapLength = args[i + 1];
                i += 2;
            } else if (name == "fftlength") {
                if (i + 1 >= args.size())
                    throw Error("spectralFlux: FFTLength needs a value.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:nargin");
                stft.FFTLength = args[i + 1];
                i += 2;
            } else if (name == "range") {
                if (i + 1 >= args.size())
                    throw Error("spectralFlux: Range needs a value.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:nargin");
                stft.Range = args[i + 1];
                i += 2;
            } else {
                throw Error("spectralFlux: unrecognized name-value "
                            "argument '" + name + "'.",
                            0, 0, "spectralFlux", "",
                            "numkit:spectralFlux:TooManyInputs");
            }
        } else {
            if (ic)
                throw Error("spectralFlux: unexpected argument at "
                            "position " + std::to_string(i + 1) + ".",
                            0, 0, "spectralFlux", "",
                            "numkit:spectralFlux:nargin");
            ic = &args[i];
            ++i;
        }
    }

    Value fc;
    Value *fcOut = (nargout >= 2) ? &fc : nullptr;
    outs[0] = spectralFluxFull(x, f, p, ic, fcOut, magnitude, stft, mr);
    if (fcOut && outs.size() >= 2) outs[1] = std::move(fc);
}

} // namespace detail
} // namespace numkit::audio
