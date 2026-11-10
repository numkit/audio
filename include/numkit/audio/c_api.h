/* include/numkit/audio/c_api.h
 *
 * Self-contained C-ABI for the `numkit/audio` toolbox.
 *
 * Supports both:
 *   1. Pre-resolved O(1) dispatch (`numkit_audio_resolve` + `numkit_audio_invoke`)
 *      with zero string hashing on the hot path.
 *   2. Dynamic name/method dispatch (`numkit_audio_call`) for MATLAB overloads,
 *      Name-Value pairs, and toolbox object methods.
 */
#ifndef NUMKIT_AUDIO_C_API_H
#define NUMKIT_AUDIO_C_API_H

#include <numkit/value/c_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(NUMKIT_PLUGIN_BUILD)
#    define NUMKIT_AUDIO_API __declspec(dllexport)
#  elif defined(NUMKIT_AUDIO_STATIC)
#    define NUMKIT_AUDIO_API
#  else
#    define NUMKIT_AUDIO_API __declspec(dllimport)
#  endif
#else
#  if defined(__GNUC__) && __GNUC__ >= 4
#    define NUMKIT_AUDIO_API __attribute__((visibility("default")))
#  else
#    define NUMKIT_AUDIO_API
#  endif
#endif

/* Opaque isolated `audio` toolbox context */
typedef struct numkit_audio_ctx_s numkit_audio_ctx;

/* Create an isolated `audio` toolbox context (engine-free, zero interpreter overhead). */
NUMKIT_AUDIO_API numkit_audio_ctx *numkit_audio_ctx_create(numkit_error *err);

/* Destroy `audio` toolbox context and release its resources. */
NUMKIT_AUDIO_API void numkit_audio_ctx_destroy(numkit_audio_ctx *ctx);

/* Seed the `audio` context's random number stream (MATLAB `rng(seed)` semantics). */
NUMKIT_AUDIO_API void numkit_audio_seed_rng(numkit_audio_ctx *ctx, uint64_t seed);

/* Return 1 if function `name` is registered in `audio`, 0 otherwise. */
NUMKIT_AUDIO_API int numkit_audio_has_function(const numkit_audio_ctx *ctx, const char *name);

/* Register a host-language callback (`numkit_host_fn`) into this context. */
NUMKIT_AUDIO_API int numkit_audio_register_fn(numkit_audio_ctx *ctx,
                                            const char *name,
                                            numkit_host_fn fn,
                                            void *user_data,
                                            numkit_error *err);

/* Resolve function `name` once into an O(1) function pointer (`NULL` if not found). */
NUMKIT_AUDIO_API const numkit_fn *numkit_audio_resolve(const numkit_audio_ctx *ctx,
                                                     const char *name);

/* Invoke a pre-resolved function pointer directly with zero lookup overhead. */
NUMKIT_AUDIO_API int numkit_audio_invoke(numkit_audio_ctx *ctx,
                                       const numkit_fn *fn,
                                       const numkit_val *const *args,
                                       size_t nargs,
                                       size_t nargout,
                                       numkit_val **outs,
                                       numkit_error *err);

/* Dynamically invoke `audio` function or object method `name`. */
NUMKIT_AUDIO_API int numkit_audio_call(numkit_audio_ctx *ctx,
                                     const char *name,
                                     const numkit_val *const *args,
                                     size_t nargs,
                                     size_t nargout,
                                     numkit_val **outs,
                                     numkit_error *err);

/* Format a value or `audio` class object (e.g. `sym`, `tf`, `ss`) to UTF-8 text. */
NUMKIT_AUDIO_API size_t numkit_audio_format(const numkit_audio_ctx *ctx,
                                          const numkit_val *v,
                                          char *buf,
                                          size_t buf_cap);

#ifdef __cplusplus
}
#endif

#endif /* NUMKIT_AUDIO_C_API_H */
