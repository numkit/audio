// toolboxes/audio/src/spectral/cepstral_reg.cpp
//
// Register half of the cepstral builtins: the CallContext wrappers
// cepstralCoefficients / mfcc / gtcc (arg parsing + multi-output delta
// packing) that delegate to the engine-free compute in cepstral.cpp.
// library.cpp forward-declares + registers these by name.
//
// Phase 2b compute/register split — see project_layering_refactor memory.

#include <keel/core.h>
#include <keel/format.h>
#include <keel/ref.h>
#include <keel/string.h>
#include <keel/utf8.h>
#include <numkit/audio/spectral/cepstral.hpp>

#include <numkit/sys/plugin.hpp>   // CallContext, ctx.engine->resource()
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>
namespace numkit::audio {
namespace detail {

void cepstralCoefficients_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,
                               keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    // Probed R2025b signature: cepstralCoefficients(S) plus name-value
    // pairs ('NumCoeffs', 'Rectification' — names match case-
    // insensitively with unambiguous prefixes); a positional second
    // argument is TooManyInputs in the reference.
    argcheck::checkNargin(args.size(), 1, "cepstralCoefficients");
    keel::i32 nc = 13;
    CepstralRect rect = CepstralRect::Log;
    const Value &S = args[0];
    for (keel::usize i = 1; i < args.size(); i += 2) {
        if (!(args[i].isChar() || args[i].isString()) || i + 1 >= args.size())
            throw Error(keel::format("cepstralCoefficients: expected a name-value pair "
                                     "at argument {}.", i + 1),
                        0, 0, "cepstralCoefficients", "",
                        "numkit:cepstralCoefficients:nargin");
        keel::String name = args[i].toString();
        name.to_ascii_lower_in_place();
        if (name == "numcoeffs" || name == "num") {
            const keel::i32 argPos = keel::as<keel::i32>(i + 2);
            nc = keel::as<keel::i32>(argcheck::expectedPositiveInteger(
                argcheck::expectedReal(args[i + 1], argPos, "NumCoeffs",
                                       "cepstralCoefficients"),
                argPos, "NumCoeffs", "cepstralCoefficients"));
        } else if (name == "rectification" || name == "rect") {
            keel::String val = args[i + 1].toString();
            val.to_ascii_lower_in_place();
            if (val == "log") rect = CepstralRect::Log;
            else if (val == "cubic-root") rect = CepstralRect::CubicRoot;
            else if (val == "none") rect = CepstralRect::None;
            else
                throw Error(keel::format("cepstralCoefficients: Rectification must be "
                                         "'log', 'cubic-root' or 'none' (got '{}').", val),
                            0, 0, "cepstralCoefficients", "",
                            "numkit:cepstralCoefficients:rectification");
        } else {
            throw Error(keel::format("cepstralCoefficients: unrecognized name-value "
                                     "argument '{}' (expected NumCoeffs or Rectification).", name),
                        0, 0, "cepstralCoefficients", "",
                        "numkit:cepstralCoefficients:TooManyInputs");
        }
    }
    outs[0] = cepstralCoefficients(S, nc, rect, ctx.engine->allocator());
}

void mfcc_reg(keel::ArrayRef<Value> args, keel::usize nargout,
              keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 2, 3, "mfcc");
    keel::i32 nc = 13;
    if (args.size() >= 3) nc = argcheck::expectedPositiveInteger(
        argcheck::expectedReal(args[2], 3, "numCoeffs", "mfcc"), 3, "numCoeffs", "mfcc");
    auto [c, d, dd] = mfcc(args[0], args[1].toScalar(), nc,
                            ctx.engine->allocator());
    outs[0] = c;
    if (nargout >= 2 && outs.size() >= 2) outs[1] = d;
    if (nargout >= 3 && outs.size() >= 3) outs[2] = dd;
}

void gtcc_reg(keel::ArrayRef<Value> args, keel::usize nargout,
              keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 2, 3, "gtcc");
    keel::i32 nc = 13;
    if (args.size() >= 3) nc = argcheck::expectedPositiveInteger(
        argcheck::expectedReal(args[2], 3, "numCoeffs", "gtcc"), 3, "numCoeffs", "gtcc");
    auto [c, d, dd] = gtcc(args[0], args[1].toScalar(), nc,
                            ctx.engine->allocator());
    outs[0] = c;
    if (nargout >= 2 && outs.size() >= 2) outs[1] = d;
    if (nargout >= 3 && outs.size() >= 3) outs[2] = dd;
}

} // namespace detail
} // namespace numkit::audio
