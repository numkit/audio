// modules/audio/tests/arity_bounds_test.cpp
//
// Max-arity rejection guards for the audio domain (argcheck migration).
// Every bounded contract gets a max+1 call that must fail with
// numkit:<fn>:maxnargin.

#include <numkit/core/standard_engine.hpp>
#include <numkit/core/engine.hpp>
#include <numkit/value/error.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

struct Case
{
    const char *fn;
    int maxArgs;
};

const std::vector<Case> kBounds = {
    {"audioDelta", 3},
    {"audioinfo", 1},
    {"audioread", 3},
    {"bark2hz", 1},
    {"erb2hz", 1},
    {"gtcc", 3},
    {"harmonicRatio", 2},
    {"hz2bark", 1},
    {"hz2erb", 1},
    {"hz2mel", 1},
    {"mel2hz", 1},
    {"melSpectrogram", 3},
    {"mfcc", 3},
    {"midiinfo", 1},
    {"midiread", 1},
    {"midiwrite", 4},
    {"phon2sone", 2},
    {"sone2phon", 2},
};

std::string callExpr(const Case &c)
{
    std::string s = c.fn;
    s += '(';
    for (int i = 0; i <= c.maxArgs; ++i) {
        if (i)
            s += ", ";
        s += '1';
    }
    s += ')';
    return s;
}

} // namespace

class AudioArityBoundsTest : public ::testing::TestWithParam<Case>
{
public:
    numkit::StandardEngine engine;
};

TEST_P(AudioArityBoundsTest, RejectsMaxPlusOne)
{
    const Case &c = GetParam();
    try {
        engine.eval(callExpr(c));
        FAIL() << "expected maxnargin error: " << callExpr(c);
    } catch (const numkit::Error &e) {
        EXPECT_EQ(e.identifier(), std::string("numkit:") + c.fn + ":maxnargin")
            << callExpr(c) << "\n  got: " << e.what();
    }
}

INSTANTIATE_TEST_SUITE_P(AudioArityBounds, AudioArityBoundsTest,
                         ::testing::ValuesIn(kBounds));
