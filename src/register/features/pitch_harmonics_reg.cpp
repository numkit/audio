// toolboxes/audio/src/features/pitch_harmonics_reg.cpp
//
// Register half of the pitch / harmonicRatio builtins: the CallContext
// wrappers (Method / Range name-value parsing, method dispatch) that
// delegate to the engine-free compute in pitch_harmonics.cpp. library.cpp
// forward-declares + registers these by name.
//
// Phase 2b compute/register split — see project_layering_refactor memory.

#include <keel/core.h>
#include <keel/ref.h>
#include <keel/string.h>
#include <keel/utf8.h>
#include <numkit/audio/features/pitch_harmonics.hpp>

#include <numkit/sys/plugin.hpp>   // CallContext, ctx.engine->resource()
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>

namespace numkit::audio {
namespace detail {

// pitch dispatches on optional Method arg. Recognized:
//   'NCF' (default, cycle E)
//   'CEP' (cycle K)
//   'PEF' (cycle K-2)
// 'LHS'/'SRH' deferred — fall through to NCF for now.
// Cycle L (partial) added 'Range' NV pair → minF/maxF override default
// [50, 400] Hz pitch search range.
// Calling convention supports Name-Value pairs:
//   pitch(x, fs)                                — NCF default
//   pitch(x, fs, 'Method', 'CEP')
//   pitch(x, fs, 'Range', [80 250])             — restrict to speech
//   pitch(x, fs, 'Method', 'PEF', 'Range', [...])
void pitch_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,
               keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 2, "pitch");
    keel::String method = keel::String::from("NCF");
    keel::f64 minF = 50.0, maxF = 400.0;
    // Parse Name-Value pairs starting at args[2].
    for (keel::usize i = 2; i + 1 < args.size(); i += 2) {
        if (args[i].type() != ValueType::CHAR && args[i].type() != ValueType::STRING)
            continue;
        keel::String name = args[i].toString();
        name.to_ascii_lower_in_place();
        if (name == "method") {
            keel::String m = args[i + 1].toString();
            m.to_ascii_upper_in_place();
            method = keel::move(m);
        } else if (name == "range") {
            const Value &r = args[i + 1];
            if (!r.isNumeric() || r.isComplex() || r.numel() != 2)
                throw Error("pitch: Range must be a 2-element real numeric vector [lo hi]",
                            0, 0, "pitch", "", "numkit:pitch:BadRange");
            minF = r.elemAsDouble(0);
            maxF = r.elemAsDouble(1);
            if (!keel::is_finite(minF) || !keel::is_finite(maxF))
                throw Error("pitch: Range elements must be finite",
                            0, 0, "pitch", "", "numkit:pitch:BadRange");
        }
    }
    const keel::f64 fs = args[1].toScalar();
    if (method == "CEP")
        outs[0] = pitchCEP(args[0], fs, minF, maxF, ctx.engine->allocator());
    else if (method == "PEF")
        outs[0] = pitchPEF(args[0], fs, minF, maxF, ctx.engine->allocator());
    else if (method == "LHS")
        outs[0] = pitchLHS(args[0], fs, minF, maxF, ctx.engine->allocator());
    else if (method == "SRH")
        outs[0] = pitchSRH(args[0], fs, minF, maxF, ctx.engine->allocator());
    else
        outs[0] = pitch(args[0], fs, minF, maxF, ctx.engine->allocator());
}

void harmonicRatio_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,
                       keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 2, 2, "harmonicRatio");
    outs[0] = harmonicRatio(args[0], args[1].toScalar(), ctx.engine->allocator());
}

} // namespace detail
} // namespace numkit::audio
