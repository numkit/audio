// toolboxes/audio/tests/known_bugs_test.cpp
//
// One DISABLED_ test per OPEN bug in bugs/opened/audio/*.md. Disabled
// until fixed; remove `DISABLED_` to turn into a live regression guard.
// MATLAB R2025b reference values.

#include <numkit/bundle/standard_engine.hpp>
#include <numkit/core/engine.hpp>
#include <gtest/gtest.h>

class AudioKnownBug : public ::testing::Test
{
public:
    numkit::StandardEngine engine;
    numkit::Value eval(const std::string &c) { return engine.eval(c); }
    double evalScalar(const std::string &c) { return eval(c).toScalar(); }
};

// bugs/opened/audio/spectral-flux-multichannel-time-form.md — the (x, fs)
// time form must treat columns as channels: a 1x160 row is 160 channels
// of one sample and rejects (insufficientSignalLength); nk flattens it
// into one channel and computes.
TEST_F(AudioKnownBug, SpectralFluxRowTimeFormRejects)
{
    eval("xv = cos(2*pi*(0:159)/40);");
    bool threw = false;
    try { eval("y = spectralFlux(xv, 1600);"); }
    catch (...) { threw = true; }
    EXPECT_TRUE(threw);
}

// bugs/closed/audio/spectral-family-stft-options.md — shared Window /
// OverlapLength / FFTLength / Range NV options on all spectral* descriptors.
TEST_F(AudioKnownBug, SpectralFamilyStftOptions)
{
    eval("xc = cos(2*pi*(0:159)'/40);");
    eval("y1 = spectralCentroid(xc, 1600, 'Window', ones(32, 1), 'OverlapLength', 16);");
    EXPECT_EQ(static_cast<int>(evalScalar("numel(y1)")), 9);
    eval("y2 = spectralSpread(xc, 1600, 'Window', ones(32, 1), 'OverlapLength', 16, 'FFTLength', 64);");
    EXPECT_EQ(static_cast<int>(evalScalar("numel(y2)")), 9);
    eval("y3 = spectralRolloffPoint(xc, 1600, 'Window', ones(32, 1), 'OverlapLength', 16);");
    EXPECT_EQ(static_cast<int>(evalScalar("numel(y3)")), 9);
}

