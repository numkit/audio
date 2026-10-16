// toolboxes/audio/tests/type_contract_test.cpp
//
// Class/arity-contract guards for the audio argcheck portion
// (2026-09-19): cepstralCoefficients name-value signature +
// Rectification, audioDelta class/ndims/initialCondition state,
// spectralFlux name-value signature + initialCondition/finalCondition +
// the probed default STFT window rule (64/32 pinned below the 128
// threshold). Reference: MATLAB R2025b, build/probe42/45/48..53.

#include <numkit/bundle/standard_engine.hpp>
#include <numkit/core/engine.hpp>
#include <gtest/gtest.h>

class AudioTypeContract : public ::testing::Test
{
public:
    numkit::StandardEngine engine;
    numkit::Value eval(const std::string &c) { return engine.eval(c); }
    double evalScalar(const std::string &c) { return eval(c).toScalar(); }
    std::string evalClass(const std::string &c) { return eval(c).toString(); }
    bool throws(const std::string &c)
    {
        try { eval(c); } catch (...) { return true; }
        return false;
    }
};

// ── cepstralCoefficients ──────────────────────────────────────────────
TEST_F(AudioTypeContract, CepstralCoefficientsNameValueSignature)
{
    eval("S4 = reshape(1:12,4,3);");
    // positional NumCoeffs is TooManyInputs in the reference
    EXPECT_TRUE(throws("cepstralCoefficients(S4, 3);"));
    // name-value (case-insensitive, prefix), default 13 coefficients
    eval("c = cepstralCoefficients(S4, 'NumCoeffs', 3);");
    EXPECT_NEAR(evalScalar("c(1,1)"), 0.690106, 1e-6);
    EXPECT_EQ(evalScalar("size(c,2)"), 3.0);
    eval("c1 = cepstralCoefficients(S4);");
    EXPECT_EQ(evalScalar("size(c1,2)"), 13.0);
    eval("cl = cepstralCoefficients(S4, 'numcoeffs', 3);");
    EXPECT_EQ(evalScalar("size(cl,2)"), 3.0);
    // Rectification: 'log' default, 'cubic-root', 'none' (string scalar ok)
    eval("cc = cepstralCoefficients(S4,'NumCoeffs',3,'Rectification','cubic-root');");
    EXPECT_NEAR(evalScalar("cc(1,1)"), 2.64479, 1e-5);
    eval("cn = cepstralCoefficients(S4,'NumCoeffs',3,'Rectification','none');");
    EXPECT_NEAR(evalScalar("cn(1,1)"), 5.0, 1e-9);
    EXPECT_TRUE(throws("cepstralCoefficients(S4,'Rectification','sqrt');"));
    // NumCoeffs validation
    EXPECT_TRUE(throws("cepstralCoefficients(S4,'NumCoeffs',1);"));
    EXPECT_TRUE(throws("cepstralCoefficients(S4,'NumCoeffs',2.5);"));
    // unknown NV name rejects
    EXPECT_TRUE(throws("cepstralCoefficients(S4,'Foo',3);"));
}

TEST_F(AudioTypeContract, CepstralCoefficientsClassAndShapeContract)
{
    eval("S4 = reshape(1:12,4,3);");
    // double/single only; complex rejects; single keeps single
    EXPECT_EQ(evalClass("class(cepstralCoefficients(single(S4)))"), "single");
    EXPECT_TRUE(throws("cepstralCoefficients(logical(S4>6));"));
    EXPECT_TRUE(throws("cepstralCoefficients(uint8(S4));"));
    EXPECT_TRUE(throws("cepstralCoefficients(int16(S4));"));
    EXPECT_TRUE(throws("cepstralCoefficients(complex(S4,1));"));
    EXPECT_TRUE(throws("cepstralCoefficients('ab');"));
    // nonnegative required for log/cubic-root; 'none' takes negatives
    EXPECT_TRUE(throws("cepstralCoefficients([1 -2 3;4 -5 6]);"));
    EXPECT_TRUE(throws("cepstralCoefficients([1 -2 3;4 -5 6],'Rectification','cubic-root');"));
    EXPECT_NEAR(evalScalar("cepstralCoefficients([1 -2 3;4 -5 6],'Rectification','none','NumCoeffs',2)(1,1)"),
                3.53553, 1e-5);
    // zero bins clamp to realmin before log10 (finite first coefficient)
    eval("cz = cepstralCoefficients([0 1 2;0 3 4],'NumCoeffs',2);");
    EXPECT_NEAR(evalScalar("cz(1,1)"), -435.088, 1e-2);
    // empties: [] → 0x13, 0x3 → 3x13 zeros
    EXPECT_EQ(evalScalar("size(cepstralCoefficients([]),2)"), 13.0);
    EXPECT_EQ(evalScalar("size(cepstralCoefficients([]),1)"), 0.0);
    EXPECT_EQ(evalScalar("size(cepstralCoefficients(zeros(0,3)),1)"), 3.0);
    // 3-D: DCT along dim 1 per trailing frame, output (d2, K, d3)
    eval("c3 = cepstralCoefficients(reshape(1:8,2,2,2),'NumCoeffs',2);");
    EXPECT_EQ(evalScalar("size(c3,1)"), 2.0);
    EXPECT_EQ(evalScalar("size(c3,2)"), 2.0);
    EXPECT_EQ(evalScalar("size(c3,3)"), 2.0);
    EXPECT_NEAR(evalScalar("c3(1,1,1)"), 0.21286, 1e-5);
    EXPECT_NEAR(evalScalar("c3(1,1,2)"), 1.04448, 1e-5);
}

// ── audioDelta ────────────────────────────────────────────────────────
TEST_F(AudioTypeContract, AudioDeltaClassNdimsAndState)
{
    eval("xc = cos(2*pi*(0:159)/40); xc = xc(:);");
    eval("d = audioDelta(xc, 9);");
    EXPECT_NEAR(evalScalar("d(1)"), 0.133333, 1e-6);
    // class follows x; logical/int/char/complex reject
    EXPECT_EQ(evalClass("class(audioDelta(single(xc),9))"), "single");
    EXPECT_TRUE(throws("audioDelta(logical(xc>0),9);"));
    EXPECT_TRUE(throws("audioDelta(int16(round(100*xc)),9);"));
    EXPECT_TRUE(throws("audioDelta('abc',9);"));
    EXPECT_TRUE(throws("audioDelta(complex(xc,1),9);"));
    // windowLength: positive odd integer > 1
    EXPECT_TRUE(throws("audioDelta(xc,2);"));
    EXPECT_TRUE(throws("audioDelta(xc,4);"));
    EXPECT_TRUE(throws("audioDelta(xc,0);"));
    EXPECT_TRUE(throws("audioDelta(xc,9.5);"));
    // 3-D filters page-wise
    eval("d3 = audioDelta(reshape(1:24,2,3,4), 3);");
    EXPECT_EQ(evalScalar("size(d3,1)"), 2.0);
    EXPECT_EQ(evalScalar("size(d3,3)"), 4.0);
    EXPECT_EQ(evalClass("class(audioDelta(single(reshape(1:24,2,3,4)),3))"), "single");
    // initialCondition: MATLAB filter zi/zf convention (values probed)
    eval("[dd, fc] = audioDelta(1:10, 3, [1 2]);");
    EXPECT_NEAR(evalScalar("dd(2)"), 3.0, 1e-12);          // x + zi(1)
    EXPECT_NEAR(evalScalar("fc(1,2)"), 2.0, 1e-12);        // b1·x + zi(2)
    EXPECT_NEAR(evalScalar("fc(2,2)"), -2.0, 1e-12);       // b2·x
    // wrong-shaped ic rejects
    EXPECT_TRUE(throws("audioDelta(ones(4,3), 3, zeros(3,3));"));
    EXPECT_TRUE(throws("audioDelta(ones(4,3), 3, zeros(2,4));"));
    EXPECT_TRUE(throws("audioDelta(1:10, 3, 'x');"));
    // finalCondition shape without ic: (wl-1) x channels of zeros
    eval("[d0, f0] = audioDelta(ones(4,3), 3);");
    EXPECT_EQ(evalScalar("size(f0,1)"), 2.0);
    EXPECT_EQ(evalScalar("size(f0,2)"), 3.0);
}

// ── spectralFlux ──────────────────────────────────────────────────────
TEST_F(AudioTypeContract, SpectralFluxSignatureAndState)
{
    eval("S4 = reshape(1:12, 4, 3); F4 = (0:3)';");
    eval("y = spectralFlux(S4, F4);");
    EXPECT_NEAR(evalScalar("y(2)"), 8.0, 1e-12);
    // NormType NV replaces any positional p; 3rd positional is ic
    eval("y1 = spectralFlux(S4, F4, 'NormType', 1);");
    EXPECT_NEAR(evalScalar("y1(2)"), 16.0, 1e-12);
    EXPECT_TRUE(throws("spectralFlux(S4, F4, 1);"));   // 1x1 ic, wrong size
    eval("yic = spectralFlux(S4, F4, zeros(4,1));");
    EXPECT_NEAR(evalScalar("yic(1)"), 5.477226, 1e-6); // ||S1 - ic||_2
    EXPECT_TRUE(throws("spectralFlux(S4, F4, zeros(4,2));"));
    // second output = final spectrum column
    eval("[yf, fc] = spectralFlux(S4, F4);");
    EXPECT_EQ(evalScalar("numel(fc)"), 4.0);
    EXPECT_NEAR(evalScalar("fc(1)"), 9.0, 1e-12);
    // class contract
    EXPECT_EQ(evalClass("class(spectralFlux(single(S4), single(F4)))"), "single");
    EXPECT_TRUE(throws("spectralFlux(logical(S4>6), F4);"));
    EXPECT_TRUE(throws("spectralFlux(int16(S4), F4);"));
    EXPECT_TRUE(throws("spectralFlux('ab', F4);"));
    EXPECT_TRUE(throws("spectralFlux(S4, complex(F4,1));"));
    // complex X computes (|Δ| inside); F length must match rows
    eval("yc = spectralFlux(complex(S4,1), F4);");
    EXPECT_NEAR(evalScalar("yc(2)"), 8.0, 1e-12);
    EXPECT_TRUE(throws("spectralFlux(S4, (0:2)');"));
    // time form: real x, nonneg fs; STFT options stub-refuse
    eval("xc = cos(2*pi*(0:159)/40); xc = xc(:);");
    EXPECT_EQ(evalClass("class(spectralFlux(single(xc),1600))"), "single");
    EXPECT_TRUE(throws("spectralFlux(complex(xc,1),1600);"));
    EXPECT_TRUE(throws("spectralFlux(xc,-1600);"));
    // STFT options land in the shared window geometry (probed R2025b:
    // FFTLength=128 → 4 frames [0 0.12891 0.02852 0.082768]; a custom
    // Window replaces rectwin; the previousSpectrum state and a second
    // output are time-form syntax errors).
    eval("yf = spectralFlux(xc,1600,'FFTLength',128);");
    EXPECT_EQ(evalScalar("numel(yf)"), 4.0);
    EXPECT_NEAR(evalScalar("yf(2)"), 0.12891, 1e-5);
    EXPECT_NO_THROW(eval("spectralFlux(xc,1600,'Window',hann(64));"));
    EXPECT_TRUE(throws("spectralFlux(xc,1600,'FFTLength',32);"));
    EXPECT_TRUE(throws("spectralFlux(xc,1600,'Window',hann(200));"));
    EXPECT_TRUE(throws("spectralFlux(xc,1600,'Range',[700 100]);"));
    EXPECT_TRUE(throws("spectralFlux(xc,1600,ones(33,1));"));
    EXPECT_TRUE(throws("[f1,f2] = spectralFlux(xc,1600);"));
    // SpectrumType 'magnitude' accepted (validatestring prefixes);
    // unknown NV rejects
    EXPECT_NO_THROW(eval("spectralFlux(xc,1600,'SpectrumType','magnitude');"));
    EXPECT_NO_THROW(eval("spectralFlux(xc,1600,'SpectrumType','mag');"));
    EXPECT_NO_THROW(eval("spectralFlux(xc,1600,'SpectrumType','m');"));
    EXPECT_TRUE(throws("spectralFlux(xc,1600,'SpectrumType','z');"));
    EXPECT_TRUE(throws("spectralFlux(xc,1600,'Foo',1);"));
}

TEST_F(AudioTypeContract, SpectralFluxDefaultWindowGeometry)
{
    // Probed R2025b default STFT: window = round(0.03·fs) with overlap
    // round(0.02·fs) once round(0.03·fs) ≥ 128, else both pinned 64/32.
    // fs=1600 → 64/32 → 4 frames of 160 samples; values bit-match probe51.
    eval("xc = cos(2*pi*(0:159)/40); xc = xc(:);");
    eval("y = spectralFlux(xc, 1600);");
    EXPECT_EQ(evalScalar("numel(y)"), 4.0);
    EXPECT_NEAR(evalScalar("y(2)"), 0.121637, 1e-6);
    EXPECT_NEAR(evalScalar("y(3)"), 0.02691, 1e-6);
    // fs=8000 → window 240 > 160 samples → insufficientSignalLength
    EXPECT_TRUE(throws("spectralFlux(xc, 8000);"));
    // magnitude spectrum normalization (values probed round 8)
    eval("ym = spectralFlux(xc, 1600, 'SpectrumType','magnitude');");
    EXPECT_NEAR(evalScalar("ym(2)"), 0.281178, 1e-6);
}
