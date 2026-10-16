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
