# Keep SDL activity and related JNI bridge methods.
-keep class org.libsdl.app.** { *; }

# N16: rustls-platform-verifier's Java side, called only through JNI (net/src/android.rs).
-keep, includedescriptorclasses class org.rustls.platformverifier.** { *; }
