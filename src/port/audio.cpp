#include "port/audio.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <aurora/lib/logging.hpp>
#include <dolphin/ai.h>
#include <musyx/musyx.h>
#include <musyx/pc.h>
#include <port/settings.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// N112: the browser build has no threads (no pthreads, Asyncify): no MusyX device thread and no stream read thread.
// SDL's Emscripten backend asks for audio from the page's audio callback, so a stream callback renders there, and the
// stream reads are serviced on game frames (partyboard_audio_frame).
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define PB_AUDIO_THREADS 0
#else
#define PB_AUDIO_THREADS 1
#endif

namespace {

aurora::Module Log("partyboard::audio");

std::recursive_mutex sAudioMutex;

AIDCallback sDmaCallback = nullptr;
s32 sMasterVolume = 100;

bool sDeviceOpened = false;
bool sDeviceStarted = false;
std::atomic<bool> sBackground{false};

// N112: MusyX's clock (its 5 ms cycles: sound ends, fades, stream positions) only moves when something renders. With no
// device, a paused one, or a browser tab before its first gesture nothing pulls, and offline the game waits on
// sounds that never end (star.c, boo_house.c, item.c: while (msmStreamGetStatus(...) != 0)). The game thread renders
// the missing time (into nothing) once the output has stood still for a while.
constexpr auto kStallTime = std::chrono::milliseconds(250);
u64 sClockRendered = 0;
std::chrono::steady_clock::time_point sClockProgress;
bool sClockSet = false;

struct StreamRead {
    DVDFileInfo *fileInfo;
    void *addr;
    s32 length;
    s32 offset;
    DVDCallback callback;
};

std::mutex sReadMutex;
std::condition_variable sReadCond;
std::deque<StreamRead> sReadQueue;
bool sReadStop = false;
#if PB_AUDIO_THREADS
std::thread sReadThread;
#else
SDL_AudioStream *sWebStream = nullptr;
#endif

void output_callback(s16 *output, size_t frames, u32 rate, u32 channels) {
    (void)rate;

    // 5 ms MusyX cycle
    if (sDmaCallback != nullptr) {
        sDmaCallback();
    }

    if (sMasterVolume < 100) {
        const s32 gain = sMasterVolume * 32768 / 100;
        for (size_t i = 0; i < frames * channels; ++i) {
            output[i] = static_cast<s16>((output[i] * gain) >> 15);
        }
    }
}

void stream_read_run(const StreamRead &request) {
    const s32 result = DVDReadPrio(request.fileInfo, request.addr, request.length, request.offset, 2);
    std::lock_guard audioLock(sAudioMutex);
    if (request.callback != nullptr) {
        request.callback(result, request.fileInfo);
    }
}

#if PB_AUDIO_THREADS
void stream_read_loop() {
    std::unique_lock lock(sReadMutex);
    while (true) {
        sReadCond.wait(lock, [] { return sReadStop || !sReadQueue.empty(); });
        if (sReadStop) {
            break;
        }
        const StreamRead request = sReadQueue.front();
        sReadQueue.pop_front();
        lock.unlock();

        stream_read_run(request);

        lock.lock();
    }
}
#else
// The reads queued so far, on the calling thread (the only one).
void stream_read_service() {
    while (true) {
        StreamRead request;
        {
            std::lock_guard lock(sReadMutex);
            if (sReadStop || sReadQueue.empty()) {
                return;
            }
            request = sReadQueue.front();
            sReadQueue.pop_front();
        }
        stream_read_run(request);
    }
}

// SDL wants `additional` more bytes now: render that much (whole frames) and queue it.
void SDLCALL web_stream_callback(void *userdata, SDL_AudioStream *stream, int additional, int total) {
    (void)userdata;
    (void)total;
    static s16 buffer[1024 * 8];

    if (!sDeviceStarted) {
        return;
    }
    stream_read_service();
    const SND_PC_RENDER_INFO info = sndPCGetRenderInfo();
    const int frameBytes = static_cast<int>(info.channels * sizeof(s16));
    int frames = (additional + frameBytes - 1) / frameBytes;
    while (frames > 0) {
        const int n = std::min(frames, static_cast<int>(sizeof(buffer) / frameBytes));
        if (!sndPCRender(buffer, n) || !SDL_PutAudioStreamData(stream, buffer, n * frameBytes)) {
            return;
        }
        frames -= n;
    }
}
#endif

// See sClockRendered. The output stood still for kStallTime (or there is none): render the time since it last moved.
void keep_clock() {
    using namespace std::chrono;
    static s16 scratch[1024 * 8];

    const auto now = steady_clock::now();
    const SND_PC_RENDER_INFO info = sndPCGetRenderInfo();
    if (!sClockSet || sBackground.load() || info.renderedFrames != sClockRendered) {
        sClockSet = true;
        sClockRendered = info.renderedFrames;
        sClockProgress = now;
        return;
    }
    if (sDeviceStarted && now - sClockProgress < kStallTime) {
        return;
    }
    // At most a second: after a hold (debugger, a long load) the clock jumps, it does not race through minutes.
    u64 frames = std::min<u64>(duration_cast<microseconds>(now - sClockProgress).count() * info.mixRate / 1000000,
                               info.mixRate);
    const u64 chunk = sizeof(scratch) / sizeof(scratch[0]) / info.channels;
    while (frames > 0) {
        const u64 n = std::min(frames, chunk);
        if (!sndPCRender(scratch, n)) {
            break;
        }
        frames -= n;
    }
    sClockRendered = sndPCGetRenderInfo().renderedFrames;
    sClockProgress = now;
}

// Mobile: no sound in the background; the game loop holds there too (N110). Seen synchronously on iOS (UIKit thread).
bool SDLCALL app_event_watch(void *userdata, SDL_Event *event) {
    (void)userdata;
    switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        if (!sBackground.exchange(true) && sDeviceStarted) {
#if PB_AUDIO_THREADS
            sndPCPauseAudio(true);
#else
            SDL_PauseAudioStreamDevice(sWebStream);
#endif
            Log.info("Paused for the background");
        }
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        if (sBackground.exchange(false) && sDeviceStarted) {
#if PB_AUDIO_THREADS
            sndPCPauseAudio(false);
#else
            SDL_ResumeAudioStreamDevice(sWebStream);
#endif
            Log.info("Resumed in the foreground");
        }
        break;
    default:
        break;
    }
    return true;
}

#if !PB_AUDIO_THREADS
bool web_open_audio(SND_PC_CONFIG *obtained) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }
    sWebStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, web_stream_callback, nullptr);
    if (sWebStream == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_AudioSpec device{};
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sWebStream), &device, nullptr)) {
        device.freq = 48000;
    }
    SND_PC_CONFIG config{static_cast<u32>(std::clamp(device.freq, 8000, 96000)), 2};
    const SDL_AudioSpec input{SDL_AUDIO_S16, 2, static_cast<int>(config.mixRate)};
    if (!SDL_SetAudioStreamFormat(sWebStream, &input, nullptr) || !sndPCConfigure(&config)) {
        SDL_DestroyAudioStream(sWebStream);
        sWebStream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    *obtained = config;
    // Autoplay: the page's AudioContext starts suspended. SDL resumes it once the page has seen a gesture; this also
    // resumes it on every later gesture after the browser suspended or interrupted it (iOS Safari after a call).
    EM_ASM({
        const resume = () => {
            const ctx = Module['SDL3'] && Module['SDL3'].audioContext;
            if (ctx && ctx.state !== 'running' && ctx.state !== 'closed') {
                ctx.resume();
            }
        };
        for (const type of ['pointerdown', 'keydown', 'touchend']) {
            document.addEventListener(type, resume, {capture: true});
        }
    });
    return true;
}
#endif

}

extern "C" {

void partyboard_audio_lock(void) { sAudioMutex.lock(); }

void partyboard_audio_unlock(void) { sAudioMutex.unlock(); }

void partyboard_audio_pre_init(void) {
    sMasterVolume = std::clamp(partyboard::getSettings().audio.masterVolume.getValue(), 0, 100);
    sndPCSetSynchronization(partyboard_audio_lock, partyboard_audio_unlock);

#if defined(__APPLE__)
    // iOS: Playback, so the silent switch does not mute the game (user's call, after N112's SoloAmbient did), and the
    // hint drops SDL's default MixWithOthers so the game's music replaces other apps' audio.
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "AVAudioSessionCategoryPlayback");
#endif

    SND_PC_CONFIG obtained{};
#if PB_AUDIO_THREADS
    sDeviceOpened = sndPCOpenAudio(nullptr, &obtained);
#else
    sDeviceOpened = web_open_audio(&obtained);
#endif
    if (sDeviceOpened) {
        Log.info("Audio output: {} Hz, {} channels", obtained.mixRate, obtained.channels);
    } else {
        Log.warn("Unable to open an audio device, sound will be muted: {}", SDL_GetError());
    }
}

void partyboard_audio_start(void) {
    sndPCSetOutputCallback(output_callback);
    SDL_AddEventWatch(app_event_watch, nullptr);

    {
        std::lock_guard lock(sReadMutex);
        sReadStop = false;
    }
#if PB_AUDIO_THREADS
    sReadThread = std::thread(stream_read_loop);
#endif

    if (sDeviceOpened) {
#if PB_AUDIO_THREADS
        sDeviceStarted = sndPCStartAudio();
#else
        sDeviceStarted = true;
        sDeviceStarted = SDL_ResumeAudioStreamDevice(sWebStream);
#endif
        if (sDeviceStarted) {
            if (sBackground.load()) {
#if PB_AUDIO_THREADS
                sndPCPauseAudio(true);
#else
                SDL_PauseAudioStreamDevice(sWebStream);
#endif
            }
            return;
        }
        Log.warn("Unable to start audio playback, sound will be muted: {}", SDL_GetError());
    }
    // No device: keep_clock renders on game frames.
}

void partyboard_audio_frame(void) {
    if (!sndIsInstalled()) {
        return;
    }
#if !PB_AUDIO_THREADS
    stream_read_service();
#endif
    keep_clock();
}

void partyboard_audio_shutdown(void) {
    SDL_RemoveEventWatch(app_event_watch, nullptr);
    // stop rendering first
    if (sDeviceOpened) {
#if PB_AUDIO_THREADS
        sndPCStopAudio();
#else
        SDL_DestroyAudioStream(sWebStream);
        sWebStream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
        sDeviceOpened = sDeviceStarted = false;
    }
    {
        std::lock_guard lock(sReadMutex);
        sReadStop = true;
        sReadQueue.clear();
    }
#if PB_AUDIO_THREADS
    if (sReadThread.joinable()) {
        sReadCond.notify_one();
        sReadThread.join();
    }
#endif
}

BOOL partyboard_audio_read_async(DVDFileInfo *fileInfo, void *addr, s32 length, s32 offset, DVDCallback callback) {
    // DVDGetCommandBlockStatus
    fileInfo->cb.state = DVD_STATE_BUSY;
    {
        std::lock_guard lock(sReadMutex);
        sReadQueue.push_back({fileInfo, addr, length, offset, callback});
    }
    sReadCond.notify_one();
    return TRUE;
}

void AIInit(u8 *stack) { (void)stack; }

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    std::lock_guard lock(sAudioMutex);
    const AIDCallback old = sDmaCallback;
    sDmaCallback = callback;
    return old;
}

void AISetStreamPlayState(u32 state) { (void)state; }
void AISetStreamVolLeft(u8 vol) { (void)vol; }
void AISetStreamVolRight(u8 vol) { (void)vol; }

}
