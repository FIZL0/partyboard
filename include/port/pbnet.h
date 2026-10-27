#pragma once

/*
 * C ABI of the `pbnet` Rust crate (net/). Hand-written: keep in sync with the
 * `extern "C"` fns in net/src/lib.rs.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Version of pbnet as a NUL-terminated string, e.g. "0.1.0".
 * Never NULL; points to static storage (do not free or modify). Thread-safe.
 */
const char *pbnet_version(void);

#ifdef __cplusplus
}
#endif
