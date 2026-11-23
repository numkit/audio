// toolboxes/audio/src/scale/freq_scales_reg.cpp
//
// Register half of the frequency-scale conversions: the CallContext
// builtins hz2mel / mel2hz / hz2bark / bark2hz / hz2erb / erb2hz +
// phon2sone / sone2phon (ISO 532-1/532-2 select) that delegate to the
// engine-free compute in freq_scales.cpp. isStandard532_2 is a
// register-side parser. library.cpp forward-declares + registers these.
//
// Compute reconciled to the canonical mr-last API (matching the header)
// as part of this split. Phase 2b — see project_layering_refactor memory.

#include <keel/core.h>
#include <keel/ref.h>
#include <keel/string.h>
#include <keel/utf8.h>
#include <numkit/audio/scale/freq_scales.hpp>

#include <numkit/sys/plugin.hpp>   // CallContext, ctx.engine->resource()
#include <numkit/value/argcheck.hpp>
#include <numkit/value/error.hpp>

namespace numkit::audio {
namespace detail {

#define NUMKIT_ELEM_REG(FN)                                                          \
    void FN##_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,                    \
                  keel::MutArrayRef<Value> outs, CallContext &ctx)                            \
    {                                                                            \
        argcheck::checkNargin(args.size(), 1, 1, #FN);                           \
        outs[0] = FN(args[0], ctx.engine->allocator());                           \
    }

NUMKIT_ELEM_REG(hz2mel)
NUMKIT_ELEM_REG(mel2hz)
NUMKIT_ELEM_REG(hz2bark)
NUMKIT_ELEM_REG(bark2hz)
NUMKIT_ELEM_REG(hz2erb)
NUMKIT_ELEM_REG(erb2hz)

#undef NUMKIT_ELEM_REG

// phon2sone / sone2phon take an optional second arg = "ISO 532-1"
// (default) or "ISO 532-2". Cycle M added the ISO 532-2 path.
namespace {
bool isStandard532_2(const Value &v)
{
    if (v.type() == ValueType::CHAR || v.type() == ValueType::STRING) {
        keel::String lower = v.toString();
        // Case-insensitive compare.
        lower.to_ascii_lower_in_place();
        return (lower == "iso 532-2");
    }
    return false;
}
} // anon

void phon2sone_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,
                   keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 1, 2, "phon2sone");
    bool iso532_2 = (args.size() >= 2) && isStandard532_2(args[1]);
    outs[0] = phon2sone(args[0], iso532_2, ctx.engine->allocator());
}

void sone2phon_reg(keel::ArrayRef<Value> args, keel::usize /*nargout*/,
                   keel::MutArrayRef<Value> outs, CallContext &ctx)
{
    argcheck::checkNargin(args.size(), 1, 2, "sone2phon");
    bool iso532_2 = (args.size() >= 2) && isStandard532_2(args[1]);
    outs[0] = sone2phon(args[0], iso532_2, ctx.engine->allocator());
}

} // namespace detail
} // namespace numkit::audio
