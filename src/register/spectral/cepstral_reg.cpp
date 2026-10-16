// toolboxes/audio/src/spectral/cepstral_reg.cpp
//
// Register half of the cepstral builtins: the CallContext wrappers
// cepstralCoefficients / mfcc / gtcc (arg parsing + multi-output delta
// packing) that delegate to the engine-free compute in cepstral.cpp.
// library.cpp forward-declares + registers these by name.
//
// Phase 2b compute/register split — see project_layering_refactor memory.

#include <numkit/audio/spectral/cepstral.hpp>

#include <numkit/sys/plugin.hpp>   // CallContext, ctx.engine->resource()
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>

namespace numkit::audio {
namespace detail {

void cepstralCoefficients_reg(Span<const Value> args, size_t /*nargout*/,
                               Span<Value> outs, CallContext &ctx)
{
    // Probed R2025b signature: cepstralCoefficients(S) plus name-value
    // pairs ('NumCoeffs', 'Rectification' — names match case-
    // insensitively with unambiguous prefixes); a positional second
    // argument is TooManyInputs in the reference.
    if (args.empty())
        throw Error("cepstralCoefficients: requires (S[, Name=Value])",
                    0, 0, "cepstralCoefficients", "",
                    "nk:cepstralCoefficients:nargin");
    int nc = 13;
    CepstralRect rect = CepstralRect::Log;
    const Value &S = args[0];
    for (std::size_t i = 1; i < args.size(); i += 2) {
        if (!(args[i].isChar() || args[i].isString()) || i + 1 >= args.size())
            throw Error("cepstralCoefficients: expected a name-value pair "
                        "at argument " + std::to_string(i + 1) + ".",
                        0, 0, "cepstralCoefficients", "",
                        "nk:cepstralCoefficients:nargin");
        std::string name = args[i].toString();
        for (auto &c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == "numcoeffs" || name == "num") {
            nc = argcheck::expectedPositiveInteger(
                argcheck::expectedReal(args[i + 1], i + 2, "NumCoeffs",
                                       "cepstralCoefficients"),
                i + 2, "NumCoeffs", "cepstralCoefficients");
            if (nc <= 1)
                throw Error("cepstralCoefficients: NumCoeffs must be an "
                            "integer greater than 1 (got "
                            + std::to_string(nc) + ").",
                            0, 0, "cepstralCoefficients", "",
                            "nk:cepstralCoefficients:BadN");
        } else if (name == "rectification" || name == "rect") {
            std::string val = args[i + 1].toString();
            for (auto &c : val)
                c = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c)));
            if (val == "log") rect = CepstralRect::Log;
            else if (val == "cubic-root") rect = CepstralRect::CubicRoot;
            else if (val == "none") rect = CepstralRect::None;
            else
                throw Error("cepstralCoefficients: Rectification must be "
                            "'log', 'cubic-root' or 'none' (got '" + val + "').",
                            0, 0, "cepstralCoefficients", "",
                            "nk:cepstralCoefficients:rectification");
        } else {
            throw Error("cepstralCoefficients: unrecognized name-value "
                        "argument '" + name + "' (expected NumCoeffs or "
                        "Rectification).",
                        0, 0, "cepstralCoefficients", "",
                        "nk:cepstralCoefficients:TooManyInputs");
        }
    }
    outs[0] = cepstralCoefficients(S, nc, rect, ctx.engine->resource());
}

void mfcc_reg(Span<const Value> args, size_t nargout,
              Span<Value> outs, CallContext &ctx)
{
    if (args.size() < 2)
        throw Error("mfcc: requires (x, fs [, NumCoeffs])",
                    0, 0, "mfcc", "", "nk:mfcc:nargin");
    int nc = 13;
    if (args.size() >= 3) nc = argcheck::expectedPositiveInteger(
        argcheck::expectedReal(args[2], 3, "numCoeffs", "mfcc"), 3, "numCoeffs", "mfcc");
    auto [c, d, dd] = mfcc(args[0], args[1].toScalar(), nc,
                            ctx.engine->resource());
    outs[0] = c;
    if (nargout >= 2 && outs.size() >= 2) outs[1] = d;
    if (nargout >= 3 && outs.size() >= 3) outs[2] = dd;
}

void gtcc_reg(Span<const Value> args, size_t nargout,
              Span<Value> outs, CallContext &ctx)
{
    if (args.size() < 2)
        throw Error("gtcc: requires (x, fs [, NumCoeffs])",
                    0, 0, "gtcc", "", "nk:gtcc:nargin");
    int nc = 13;
    if (args.size() >= 3) nc = argcheck::expectedPositiveInteger(
        argcheck::expectedReal(args[2], 3, "numCoeffs", "gtcc"), 3, "numCoeffs", "gtcc");
    auto [c, d, dd] = gtcc(args[0], args[1].toScalar(), nc,
                            ctx.engine->resource());
    outs[0] = c;
    if (nargout >= 2 && outs.size() >= 2) outs[1] = d;
    if (nargout >= 3 && outs.size() >= 3) outs[2] = dd;
}

} // namespace detail
} // namespace numkit::audio
