// N111: the on-screen game pad's vibration (include/port/touch_haptics.h).
//
// SDL's haptic API is not used: on Android it only reaches the phone's vibrator as an amplitude one-shot (no
// predefined click/tick effects), and on iOS it drives game controllers' rumble, not the Taptic Engine. Each
// platform's own API is called instead.

#include "port/touch_haptics.h"

#include "port/settings.h"

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten/em_js.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "../ios/Haptics.h"
#endif
#endif

namespace {

#if defined(__ANDROID__)
// PartyBoardActivity.touchHaptic(int): the Vibrator calls. The class comes from the activity object (FindClass on
// SDL's thread sees only the system class loader), looked up once.
void android_haptic(int kind) noexcept
{
    static jclass sClass = nullptr;
    static jmethodID sMethod = nullptr;
    static bool sFailed = false;
    if (sFailed) {
        return;
    }
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) {
        return;
    }
    if (sMethod == nullptr) {
        auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (activity == nullptr) {
            env->ExceptionClear();
            return;
        }
        jclass local = env->GetObjectClass(activity);
        env->DeleteLocalRef(activity);
        jmethodID method = local != nullptr ? env->GetStaticMethodID(local, "touchHaptic", "(I)V") : nullptr;
        if (method == nullptr || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (local != nullptr) {
                env->DeleteLocalRef(local);
            }
            sFailed = true;
            return;
        }
        sClass = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        sMethod = method;
    }
    env->CallStaticVoidMethod(sClass, sMethod, static_cast<jint>(kind));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}
#elif defined(__EMSCRIPTEN__)
// Android browsers vibrate; iOS Safari and desktop browsers have no navigator.vibrate (or ignore it).
EM_JS(void, web_touch_haptic, (int ms), {
    if (typeof navigator !== "undefined" && typeof navigator.vibrate === "function") {
        try {
            navigator.vibrate(ms);
        } catch (e) {
        }
    }
});
#endif

} // namespace

extern "C" void touch_haptic(TouchHapticKind kind)
{
    if (!partyboard::getSettings().game.touchHaptics.getValue()) {
        return;
    }
#if defined(__ANDROID__)
    android_haptic(static_cast<int>(kind));
#elif defined(__EMSCRIPTEN__)
    web_touch_haptic(kind == TOUCH_HAPTIC_PRESS ? 15 : 6);
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    PartyBoard_iOS_ImpactHaptic(kind == TOUCH_HAPTIC_PRESS ? 1 : 0);
#else
    (void)kind;
#endif
}
