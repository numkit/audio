#include <numkit/audio/c_api.h>
#include <gtest/gtest.h>

TEST(NumkitAudioCApi, Hz2MelRoundtrip)
{
    const int64_t base = numkit_debug_live_values();
    numkit_error err{};
    numkit_audio_ctx *ctx = numkit_audio_ctx_create(&err);
    ASSERT_NE(ctx, nullptr) << err.message;

    numkit_val *hz = numkit_val_scalar(1000.0);
    const numkit_val *a1[1] = {hz};
    numkit_val *mel = nullptr;
    ASSERT_EQ(numkit_audio_call(ctx, "hz2mel", a1, 1, 1, &mel, &err), 0) << err.message;

    const numkit_fn *mel2hzFn = numkit_audio_resolve(ctx, "mel2hz");
    ASSERT_NE(mel2hzFn, nullptr);
    const numkit_val *a2[1] = {mel};
    numkit_val *hzBack = nullptr;
    ASSERT_EQ(numkit_audio_invoke(ctx, mel2hzFn, a2, 1, 1, &hzBack, &err), 0) << err.message;
    EXPECT_NEAR(numkit_val_as_double(hzBack, &err), 1000.0, 1e-9);

    numkit_val_release(hzBack);
    numkit_val_release(mel);
    numkit_val_release(hz);
    numkit_audio_ctx_destroy(ctx);
    EXPECT_EQ(numkit_debug_live_values(), base);
}
