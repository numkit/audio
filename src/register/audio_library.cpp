// src/register/audio_library.cpp

#include <keel/core.h>
#include <keel/format.h>
#include <keel/ref.h>
#include <numkit/audio/library.hpp>
#include <numkit/sys/plugin.hpp>

namespace numkit::audio::detail {
// scale/freq_scales.cpp
void hz2mel_reg   (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void mel2hz_reg   (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void hz2bark_reg  (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void bark2hz_reg  (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void hz2erb_reg   (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void erb2hz_reg   (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void phon2sone_reg(keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void sone2phon_reg(keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);

// spectral/shape_descriptors.cpp
void spectralCentroid_reg    (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralSpread_reg      (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralRolloffPoint_reg(keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralDecrease_reg    (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralSlope_reg       (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralFlux_reg        (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralCrest_reg       (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralEntropy_reg     (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralFlatness_reg    (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralKurtosis_reg    (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void spectralSkewness_reg    (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);

// spectral/melspec_delta.cpp
void melSpectrogram_reg      (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void audioDelta_reg          (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);

// spectral/cepstral.cpp
void cepstralCoefficients_reg(keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void mfcc_reg                (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void gtcc_reg                (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);

// features/pitch_harmonics.cpp
void pitch_reg               (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void harmonicRatio_reg       (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);

// io/audio_io_reg.cpp
void audioread_reg  (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void audiowrite_reg (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void audioinfo_reg  (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void midiread_reg   (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void midiwrite_reg  (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
void midiinfo_reg   (keel::ArrayRef<Value>, keel::usize, keel::MutArrayRef<Value>, CallContext &);
} // namespace numkit::audio::detail

namespace numkit {

void AudioLibrary::install(PluginContext &engine)
{
    auto reg = [&](const char *sub, const char *name, ExternalFunc fn) {
        keel::String ns = keel::format("audio.{}", sub);
        engine.registerFunction(ns.as_ref(), name, keel::move(fn));
    };

    reg("scale", "hz2mel",     &audio::detail::hz2mel_reg);
    reg("scale", "mel2hz",     &audio::detail::mel2hz_reg);
    reg("scale", "hz2bark",    &audio::detail::hz2bark_reg);
    reg("scale", "bark2hz",    &audio::detail::bark2hz_reg);
    reg("scale", "hz2erb",     &audio::detail::hz2erb_reg);
    reg("scale", "erb2hz",     &audio::detail::erb2hz_reg);
    reg("scale", "phon2sone",  &audio::detail::phon2sone_reg);
    reg("scale", "sone2phon",  &audio::detail::sone2phon_reg);

    reg("spectral", "spectralCentroid",     &audio::detail::spectralCentroid_reg);
    reg("spectral", "spectralSpread",       &audio::detail::spectralSpread_reg);
    reg("spectral", "spectralRolloffPoint", &audio::detail::spectralRolloffPoint_reg);
    reg("spectral", "spectralDecrease",     &audio::detail::spectralDecrease_reg);
    reg("spectral", "spectralSlope",        &audio::detail::spectralSlope_reg);
    reg("spectral", "spectralFlux",         &audio::detail::spectralFlux_reg);

    reg("spectral", "spectralCrest",        &audio::detail::spectralCrest_reg);
    reg("spectral", "spectralEntropy",      &audio::detail::spectralEntropy_reg);
    reg("spectral", "spectralFlatness",     &audio::detail::spectralFlatness_reg);
    reg("spectral", "spectralKurtosis",     &audio::detail::spectralKurtosis_reg);
    reg("spectral", "spectralSkewness",     &audio::detail::spectralSkewness_reg);

    reg("spectral", "melSpectrogram",       &audio::detail::melSpectrogram_reg);
    reg("features", "audioDelta",           &audio::detail::audioDelta_reg);

    reg("features", "cepstralCoefficients", &audio::detail::cepstralCoefficients_reg);
    reg("features", "mfcc",                 &audio::detail::mfcc_reg);
    reg("features", "gtcc",                 &audio::detail::gtcc_reg);

    reg("features", "pitch",                &audio::detail::pitch_reg);
    reg("features", "harmonicRatio",        &audio::detail::harmonicRatio_reg);

    // Audio I/O & MIDI
    reg("io", "audioread",  &audio::detail::audioread_reg);
    reg("io", "audiowrite", &audio::detail::audiowrite_reg);
    reg("io", "audioinfo",  &audio::detail::audioinfo_reg);
    reg("io", "midiread",   &audio::detail::midiread_reg);
    reg("io", "midiwrite",  &audio::detail::midiwrite_reg);
    reg("io", "midiinfo",   &audio::detail::midiinfo_reg);
}

} // namespace numkit
