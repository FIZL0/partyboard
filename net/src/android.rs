//! Android: the TLS of the short codes' HTTPS (`codes.rs` relay_get / relay_put), as opentennis's
//! `hst-base/src/android.rs` init_tls / tls. reqwest's default reaches for `rustls-platform-verifier`, which on Android
//! needs the JVM and the activity first (its JNI init) and panics in the request's task without them. The C side hands
//! them over from SDL at boot ([`pbnet_android_init_tls`]); until then, or should that fail, Mozilla's root list.

use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, OnceLock};

/// Whether `rustls-platform-verifier` is set up ([`pbnet_android_init_tls`]), so [`tls`] can check certificates the
/// phone's way.
static PLATFORM_TLS: AtomicBool = AtomicBool::new(false);

/// Hands the activity to `rustls-platform-verifier` (its JNI init: the VM, the activity as the context, and the
/// activity's class loader, which finds the verifier's Java classes the APK bundles, `org.rustls:rustls-platform-verifier`
/// in `platforms/android/app/build.gradle`). Call once at boot, before any short code. Returns 1 if the phone's own
/// certificate check is on, 0 if not (short codes then use the bundled roots).
///
/// It also hands the VM and the activity to `ndk-context` ([`init_ndk_context`]), whatever the TLS result: iroh's
/// interface scan (netwatch -> netdev) asks it for both on every endpoint bind (host, join, LAN browse), and without
/// them it panics, which `panic = "abort"` turns into a crash (research/journal/2026-10-27-n36-android-touch).
///
/// # Safety
///
/// `env` is SDL's `SDL_GetAndroidJNIEnv()`: a valid `JNIEnv*` of the calling thread, which is attached to the VM, or
/// null. `activity` is a valid local or global reference (`jobject`) to the app's `Context` in that `env`, e.g.
/// `SDL_GetAndroidActivity()`, or null. With either null this only returns 0. The verifier takes its own global
/// references and the VM, so the caller may delete `activity` once this returns. Not to be called at the same time
/// from two threads.
#[no_mangle]
pub unsafe extern "C" fn pbnet_android_init_tls(env: *mut c_void, activity: *mut c_void) -> i32 {
    if env.is_null() || activity.is_null() {
        eprintln!("pbnet: android: no JNI env or activity, short codes use the bundled roots");
        return 0;
    }
    // SAFETY: the caller guarantees `env` is the calling thread's valid JNIEnv for this call.
    let mut unowned = unsafe { jni22::EnvUnowned::from_raw(env.cast()) };
    let r = unowned
        .with_env_no_catch(|env| -> jni22::errors::Result<()> {
            // SAFETY: the caller guarantees `activity` is a valid reference to a Context in this env; the init takes
            // its own global reference.
            let activity = unsafe { jni22::objects::JObject::from_raw(env, activity.cast()) };
            init_ndk_context(env, &activity);
            rustls_platform_verifier::android::init_with_env(env, activity)
        })
        .into_outcome();
    match r {
        jni22::Outcome::Ok(()) => {
            PLATFORM_TLS.store(true, Ordering::Relaxed);
            1
        }
        jni22::Outcome::Err(e) => {
            eprintln!("pbnet: android: platform certificate check unavailable ({e}), using the bundled roots");
            0
        }
        jni22::Outcome::Panic(_) => 0,
    }
}

/// Gives `ndk-context` the VM and a global reference to `activity`, once per process (later calls do nothing). The
/// reference is never deleted: `ndk-context` keeps it for the life of the process. On failure netdev still panics on
/// bind, so it is logged.
fn init_ndk_context(env: &mut jni22::Env, activity: &jni22::objects::JObject) {
    static DONE: std::sync::Once = std::sync::Once::new();
    DONE.call_once(|| {
        let vm = match env.get_java_vm() {
            Ok(vm) => vm,
            Err(e) => return eprintln!("pbnet: android: no JavaVM for ndk-context ({e})"),
        };
        let context = match env.new_global_ref(activity) {
            Ok(g) => g.into_raw(),
            Err(e) => return eprintln!("pbnet: android: no global activity reference for ndk-context ({e})"),
        };
        // SAFETY: `vm` is this process's JavaVM and `context` a global reference to the activity, which is never
        // deleted, so both stay valid for the process. `Once` makes this the only call.
        unsafe { ndk_context::initialize_android_context(vm.get_raw().cast(), context.cast()) };
    });
}

/// The TLS settings of the short codes' HTTPS. Certificates are checked by the phone (`rustls-platform-verifier`:
/// Android's trust store, user-installed CAs and revocation included) once [`pbnet_android_init_tls`] has set it up;
/// otherwise Mozilla's root list (`webpki-roots`, the one iroh's relays use). Fixed by the first call.
pub fn tls() -> rustls::ClientConfig {
    static TLS: OnceLock<rustls::ClientConfig> = OnceLock::new();
    TLS.get_or_init(|| {
        let provider = Arc::new(rustls::crypto::ring::default_provider());
        let builder =
            rustls::ClientConfig::builder_with_provider(provider.clone()).with_safe_default_protocol_versions().expect("ring's protocol versions");
        let platform = PLATFORM_TLS.load(Ordering::Relaxed).then(|| rustls_platform_verifier::Verifier::new(provider)).and_then(|v| {
            v.map_err(|e| eprintln!("pbnet: android: platform certificate check: {e}, using the bundled roots")).ok()
        });
        let mut c = match platform {
            Some(v) => builder.dangerous().with_custom_certificate_verifier(Arc::new(v)).with_no_client_auth(),
            None => builder
                .with_root_certificates(rustls::RootCertStore { roots: webpki_roots::TLS_SERVER_ROOTS.to_vec() })
                .with_no_client_auth(),
        };
        c.alpn_protocols = vec![b"http/1.1".to_vec()];
        c
    })
    .clone()
}
