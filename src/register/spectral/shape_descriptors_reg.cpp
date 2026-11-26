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

#include <keel/core.h>
#include <keel/format.h>
#include <keel/ref.h>
#include <keel/string.h>
#include <keel/utf8.h>
#include <numkit/audio/spectral/shape_descriptors.hpp>

#include <numkit/sys/plugin.hpp>   // CallContext, ctx.engine->resource()
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>

namespace numkit::audio {
namespace detail {

static StftOptions parseSharedStftOptions(keel::ArrayRef<Value> args,
                                          keel::usize startIdx,
                                          const char *fn,
                                          keel::f64 *thresholdOut = nullptr)
{
    StftOptions stft;
    for (keel::usize i = startIdx; i < args.size();) {
        if (!args[i].isChar() && !args[i].isString())
            throw Error(keel::format("{}: unexpected argument at position {}.", fn, i + 1),
                        0, 0, fn, "", keel::format("numkit:{}:nargin", fn));
        keel::String name = args[i].toString();
        name.to_ascii_lower_in_place();
        if (i + 1 >= args.size())
            throw Error(keel::format("{}: '{}' needs a value.", fn, name),
                        0, 0, fn, "", keel::format("numkit:{}:nargin", fn));
        if (name == "window") {
            stft.Window = args[i + 1];
            i += 2;
        } else if (name == "overlaplength") {
            stft.OverlapLength = args[i + 1];
            i += 2;
        } else if (name == "fftlength") {
            stft.FFTLength = args[i + 1];
            i += 2;
        } else if (name == "range") {
            stft.Range = args[i + 1];
            i += 2;
        } else if (thresholdOut && name == "threshold") {
            *thresholdOut = args[i + 1].toScalar();
            i += 2;
        } else {
            throw Error(keel::format("{}: unrecognized name-value argument '{}'.", fn, name),
                        0, 0, fn, "",
                        keel::format("numkit:{}:TooManyInputs", fn));
        }
    }
    return stft;
}

// Per-fn registration adapters share the (x, f[, Name, Value]) argument shape.
#define NUMKIT_SPEC_REG(FN)                                                          \
    void FN##_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,                    \
                  keel::MutArrayRef<Value> outs, CallContext &ctx)                            \
    {                                                                            \
        argcheck::checkNargin(args.size(), 2, #FN);                              \
        StftOptions stft = parseSharedStftOptions(args, 2, #FN);                 \
        outs[0] = FN(args[0], args[1], stft, ctx.engine->allocator());            \
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

void spectralRolloffPoint_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,
                              keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 2, "spectralRolloffPoint");
    keel::f64 pct = 0.95;
    keel::usize nvStart = 2;
    if (args.size() >= 3 && !args[2].isChar() && !args[2].isString()) {
        pct = args[2].toScalar();
        nvStart = 3;
    }
    StftOptions stft = parseSharedStftOptions(args, nvStart,
                                              "spectralRolloffPoint", &pct);
    outs[0] = spectralRolloffPoint(args[0], args[1], pct, stft,
                                   ctx.engine->allocator());
}

void spectralFlux_reg(keel::ArrayRef<Value> args, keel::usize nargout,
                      keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    // Probed R2025b signature: spectralFlux(x, f[, initialCondition]
    // [, Name=Value]) with NormType ∈ {1, 2} (default 2), SpectrumType
    // ∈ {'power','magnitude'} (validatestring — unique prefixes match),
    // and the four STFT-reshaping options Window / OverlapLength /
    // FFTLength / Range (validated inside the shared computeStft port).
    argcheck::checkNargin(args.size(), 2, "spectralFlux");
    auto *mr = ctx.engine->allocator();
    const Value &x = args[0];
    const Value &f = args[1];

    const Value *ic = nullptr;
    keel::f64 p = 2.0;
    bool magnitude = false;
    StftOptions stft;
    for (keel::usize i = 2; i < args.size();) {
        if (args[i].isChar() || args[i].isString()) {
            keel::String name = args[i].toString();
            name.to_ascii_lower_in_place();
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
                if ((!v.isNumeric() && !v.isLogical()) || v.numel() != 1)
                    throw Error("spectralFlux: NormType must be a real numeric scalar.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:normType");
                p = v.elemAsDouble(0);
                i += 2;
            } else if (name == "spectrumtype" || name == "spect") {
                if (i + 1 >= args.size())
                    throw Error("spectralFlux: SpectrumType needs a value.",
                                0, 0, "spectralFlux", "",
                                "numkit:spectralFlux:nargin");
                keel::String val = args[i + 1].toString();
                val.to_ascii_lower_in_place();// validatestring: the value may be any unambiguous
                // case-insensitive prefix of a valid name.
                const bool isPow = keel::StringRef("power").starts_with(val.as_ref());
                const bool isMag = keel::StringRef("magnitude").starts_with(val.as_ref());
                if (isPow == isMag)
                    throw Error(keel::format("spectralFlux: SpectrumType must be "
                                             "'power' or 'magnitude' (got '{}').", val),
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
                throw Error(keel::format("spectralFlux: unrecognized name-value argument '{}'.", name),
                            0, 0, "spectralFlux", "",
                            "numkit:spectralFlux:TooManyInputs");
            }
        } else {
            if (ic)
                throw Error(keel::format("spectralFlux: unexpected argument at position {}.", i + 1),
                            0, 0, "spectralFlux", "",
                            "numkit:spectralFlux:nargin");
            ic = &args[i];
            ++i;
        }
    }

    Value fc;
    Value *fcOut = (nargout >= 2) ? &fc : nullptr;
    outs[0] = spectralFluxFull(x, f, p, ic, fcOut, magnitude, stft, mr);
    if (fcOut && outs.size() >= 2) outs[1] = keel::move(fc);
}

} // namespace detail
} // namespace numkit::audio
