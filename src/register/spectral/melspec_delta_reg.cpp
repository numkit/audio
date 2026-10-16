// toolboxes/audio/src/spectral/melspec_delta_reg.cpp
//
// Register half of the melSpectrogram / audioDelta builtins: the
// CallContext wrappers that delegate to the engine-free compute in
// melspec_delta.cpp. library.cpp forward-declares + registers these.
//
// melSpectrogram compute reconciled to the canonical mr-last API (matching
// the header) as part of this split. Phase 2b — see project_layering_refactor.

#include <numkit/audio/spectral/melspec_delta.hpp>

#include <numkit/sys/plugin.hpp>   // CallContext, ctx.engine->resource()
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>

#include <utility>

namespace numkit::audio {
namespace detail {

void melSpectrogram_reg(Span<const Value> args, size_t nargout,
                        Span<Value> outs, CallContext &ctx)
{
    if (args.size() < 2)
        throw Error("melSpectrogram: requires (x, fs [, NumBands])",
                    0, 0, "melSpectrogram", "", "nk:melSpectrogram:nargin");
    int numBands = 32;
    if (args.size() >= 3) numBands = argcheck::expectedPositiveInteger(
        argcheck::expectedReal(args[2], 3, "numBands", "melSpectrogram"), 3, "numBands", "melSpectrogram");
    auto [S, F, T] = melSpectrogram(args[0], args[1].toScalar(), numBands,
                                     ctx.engine->resource());
    outs[0] = S;
    if (nargout >= 2 && outs.size() >= 2) outs[1] = F;
    if (nargout >= 3 && outs.size() >= 3) outs[2] = T;
}

void audioDelta_reg(Span<const Value> args, size_t nargout,
                    Span<Value> outs, CallContext &ctx)
{
    // Probed R2025b: (x[, windowLength[, initialCondition]]) with an
    // optional second output (finalCondition).
    argcheck::checkNargin(args.size(), 1, 3, "audioDelta");
    int wl = 9;
    if (args.size() >= 2)
        wl = argcheck::expectedPositiveInteger(
            argcheck::expectedReal(args[1], 2, "windowLength", "audioDelta"),
            2, "windowLength", "audioDelta");
    const Value *ic = (args.size() >= 3 && !args[2].isEmpty())
        ? &args[2] : nullptr;
    Value fc;
    Value *fcOut = (nargout >= 2) ? &fc : nullptr;
    outs[0] = audioDeltaFull(args[0], wl, ic, fcOut,
                             ctx.engine->resource());
    if (fcOut && outs.size() >= 2) outs[1] = std::move(fc);
}

} // namespace detail
} // namespace numkit::audio
