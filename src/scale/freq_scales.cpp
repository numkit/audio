// toolboxes/audio/src/scale/freq_scales.cpp
//
// Audio frequency-scale and loudness conversions, delegated to keel::audio.

#include <keel/core.h>
#include <keel/memory.h>
#include <keel/arena.h>
#include <keel/array.h>
#include <keel/audio.h>
#include <numkit/audio/scale/freq_scales.hpp>

#include <numkit/value/argcheck.hpp>
#include <numkit/value/value.hpp>
#include <numkit/value/error.hpp>

namespace numkit::audio {

namespace {

template <typename BatchFn>
Value applyBatch(keel::Allocator *mr, const Value &x, BatchFn fn)
{
    const keel::usize N = x.numel();
    Value out;
    if (x.dims().is3D())
        out = Value::matrix3d(x.dims().rows(), x.dims().cols(),
                              x.dims().pages(), ValueType::DOUBLE, mr);
    else
        out = Value::matrix(x.dims().rows(), x.dims().cols(),
                            ValueType::DOUBLE, mr);
    if (N == 0) return out;

    keel::MutArrayRef<keel::f64> outRef(out.doubleDataMut(), N);
    if (x.type() == ValueType::DOUBLE) {
        fn(keel::ArrayRef<keel::f64>(x.doubleData(), N), outRef);
    } else {
        keel::ArenaTemp scratch = keel::get_scratch(mr);
        keel::Array<keel::f64> in(N, scratch.allocator());
        for (keel::usize i = 0; i < N; ++i) in[i] = x.elemAsDouble(i);
        fn(keel::ArrayRef<keel::f64>(in.data(), N), outRef);
    }
    return out;
}

void freq_scales_argcheck(const Value &x, const char *fn)
{
    (void)x;
    (void)fn;
}

} // namespace

Value hz2mel(const Value &hz, keel::Allocator *mr)
{
    freq_scales_argcheck(hz, "hz2mel");
    return applyBatch(mr, hz, [](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::hz_to_mel(in, out);
    });
}

Value mel2hz(const Value &mel, keel::Allocator *mr)
{
    freq_scales_argcheck(mel, "mel2hz");
    return applyBatch(mr, mel, [](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::mel_to_hz(in, out);
    });
}

Value hz2bark(const Value &hz, keel::Allocator *mr)
{
    freq_scales_argcheck(hz, "hz2bark");
    return applyBatch(mr, hz, [](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::hz_to_bark(in, out);
    });
}

Value bark2hz(const Value &bark, keel::Allocator *mr)
{
    freq_scales_argcheck(bark, "bark2hz");
    return applyBatch(mr, bark, [](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::bark_to_hz(in, out);
    });
}

Value hz2erb(const Value &hz, keel::Allocator *mr)
{
    freq_scales_argcheck(hz, "hz2erb");
    return applyBatch(mr, hz, [](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::hz_to_erb(in, out);
    });
}

Value erb2hz(const Value &erb, keel::Allocator *mr)
{
    freq_scales_argcheck(erb, "erb2hz");
    return applyBatch(mr, erb, [](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::erb_to_hz(in, out);
    });
}

Value phon2sone(const Value &phon, bool standardIs532_2, keel::Allocator *mr)
{
    freq_scales_argcheck(phon, "phon2sone");
    return applyBatch(mr, phon, [standardIs532_2, mr](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::phon_to_sone(in, out, standardIs532_2, mr);
    });
}

Value sone2phon(const Value &sone, bool standardIs532_2, keel::Allocator *mr)
{
    freq_scales_argcheck(sone, "sone2phon");
    return applyBatch(mr, sone, [standardIs532_2, mr](keel::ArrayRef<keel::f64> in, keel::MutArrayRef<keel::f64> out) {
        (void)keel::audio::sone_to_phon(in, out, standardIs532_2, mr);
    });
}

} // namespace numkit::audio
