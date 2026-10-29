//! `pbnet`: Party Board online play.
//!
//! Game-agnostic netcode linked into `dol` as a static library. The C ABI is declared by hand in
//! `include/port/pbnet.h`; every `extern "C"` fn here must have a matching prototype there.

#[cfg(all(target_os = "android", feature = "iroh"))]
pub mod android;
#[cfg(all(feature = "iroh", any(target_os = "ios", feature = "bonjour")))]
mod bonjour;
#[cfg(feature = "iroh")]
pub mod codes;
pub mod desync;
#[cfg(all(feature = "iroh", not(target_os = "ios")))]
mod dnssd;
pub mod keeper;
#[cfg(feature = "iroh")]
pub mod lan;
pub mod lobby;
pub mod lobby_c;
pub mod lockstep;
pub mod lockstep_c;
pub mod recover;
// N96: on every build: the browser's transport, and the desktop's way to meet browsers (`rtc/native.rs`).
pub mod rtc;
pub mod save;
// N139: WebRTC offers and answers through a pkarr relay under one short code.
pub mod signal;
#[cfg(feature = "iroh")]
pub mod transport;
// N96: a desktop lobby's transport, iroh or WebRTC (`link.rs`); the lobby uses it as `crate::link`.
#[cfg(feature = "iroh")]
pub mod link;
// N92: without iroh (the browser) the lobby runs on WebRTC under the same names.
#[cfg(not(feature = "iroh"))]
pub use rtc as transport;
#[cfg(not(feature = "iroh"))]
pub use rtc as link;
#[cfg(not(feature = "iroh"))]
pub use rtc::codes;

use std::ffi::{c_char, CStr};

/// The crate version, NUL-terminated so C can use it directly.
const VERSION: &CStr = match CStr::from_bytes_with_nul(concat!(env!("CARGO_PKG_VERSION"), "\0").as_bytes()) {
    Ok(s) => s,
    Err(_) => panic!("CARGO_PKG_VERSION contains a NUL byte"),
};

/// Returns the `pbnet` version as a NUL-terminated ASCII string, e.g. `"0.1.0"`.
///
/// # Safety
///
/// Safe to call from any thread at any time, including before any other `pbnet_*` call. The
/// returned pointer is to static storage: it is never null, stays valid for the life of the
/// process, and must not be written to or freed.
#[no_mangle]
pub extern "C" fn pbnet_version() -> *const c_char {
    VERSION.as_ptr()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_is_package_version() {
        let p = pbnet_version();
        assert!(!p.is_null());
        // SAFETY: pbnet_version returns a pointer to a static NUL-terminated string.
        let s = unsafe { CStr::from_ptr(p) };
        assert_eq!(s.to_str().unwrap(), env!("CARGO_PKG_VERSION"));
    }

    #[test]
    fn version_pointer_is_stable() {
        assert_eq!(pbnet_version(), pbnet_version());
    }
}
