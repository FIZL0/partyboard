# Keep SDL activity and related JNI bridge methods.
-keep class org.libsdl.app.** { *; }

# N16: rustls-platform-verifier's Java side, called only through JNI (net/src/android.rs).
-keep, includedescriptorclasses class org.rustls.platformverifier.** { *; }

# N111: PartyBoardActivity.touchHaptic, called only through JNI (src/port/ui/touch_haptics.cpp).
-keepclassmembers class com.mariopartyrd.partyboard.PartyBoardActivity { public static void touchHaptic(int); }
