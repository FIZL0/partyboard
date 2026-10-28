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
#include <port/netplay.h>
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

// musyx/runtime/snd_service.c: the seed of MusyX's random numbers (macros' RNDNOTE etc).
extern "C" u32 last_rnd;

namespace {

aurora::Module Log("partyboard::audio");

std::recursive_mutex sAudioMutex;

AIDCallback sDmaCallback = nullptr;
s32 sMasterVolume = 100;

bool sDeviceOpened = false;
bool sDeviceStarted = false;
std::atomic<bool> sBackground{false};

// N122: the port's own SDL stream on every platform (MusyX's SDL adapter rendered on its own thread, which netplay's
// frame clock can't stop without stopping the device). Its callback renders what the device asks for, except while
// sLocked: then MusyX moves only on game frames (10 cycles every 3 frames, the GC's 5 ms DSP cycles at 60 fps) and
// the game thread queues the PCM. One mix rate everywhere: a sound's end (its sample position after so many cycles)
// must not depend on the device's rate, or two peers' statuses would differ. SDL converts to the device's rate.
constexpr u32 kMixRate = 48000;
constexpr u32 kCycleFrames = kMixRate / 200;
constexpr u32 kCyclesPer3Frames = 10;
// Locked: the queue the device plays from. Under kQueueLow frames' worth (an underrun: a lockstep wait, a load) the
// game thread first queues silence up to kQueueStart frames; over kQueueHigh (turbo, a slower device clock) a frame's
// PCM is dropped.
constexpr int kQueueLow = 1;
constexpr int kQueueStart = 3;
constexpr int kQueueHigh = 8;
constexpr u32 kSettleCycles = 40;
SDL_AudioStream *sStream = nullptr;
u32 sChannels = 2;
std::atomic<bool> sLocked{false};
u64 sLockedFrames = 0;

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
// N122: the read thread is running one (sReadMutex). While sLocked the thread leaves the queue to the game thread.
bool sReadBusy = false;
#if PB_AUDIO_THREADS
std::thread sReadThread;
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
        sReadCond.wait(lock, [] { return sReadStop || (!sLocked.load() && !sReadQueue.empty()); });
        if (sReadStop) {
            break;
        }
        const StreamRead request = sReadQueue.front();
        sReadQueue.pop_front();
        sReadBusy = true;
        lock.unlock();

        stream_read_run(request);

        lock.lock();
        sReadBusy = false;
        sReadCond.notify_all();
    }
}
#endif

// The reads queued so far, on the calling thread (the browser's only one; the game thread while sLocked).
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

// SDL wants `additional` more bytes now: render that much (whole frames) and queue it. While sLocked the game thread
// queues instead (a shortfall plays as silence).
void SDLCALL stream_callback(void *userdata, SDL_AudioStream *stream, int additional, int total) {
    (void)userdata;
    (void)total;
    static s16 buffer[1024 * 8];

    std::lock_guard audioLock(sAudioMutex);
    if (!sDeviceStarted || sLocked.load()) {
        return;
    }
#if !PB_AUDIO_THREADS
    stream_read_service();
#endif
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

// One 5 ms cycle on the calling thread, under the lock, into `out` (kCycleFrames frames) or nothing. Reads the last
// cycle asked for are done first, as the drive would have answered them by now.
void locked_cycle(s16 *out) {
    static s16 scratch[kCycleFrames * 8];
    stream_read_service();
    sndPCRender(out != nullptr ? out : scratch, kCycleFrames);
}

// Locked: this game frame's cycles, then the PCM to the device (outside the lock: SDL's stream lock is taken before
// ours in stream_callback).
void locked_frame() {
    static s16 buffer[4 * kCycleFrames * 8];
    const u64 cycles = (sLockedFrames + 1) * kCyclesPer3Frames / 3 - sLockedFrames * kCyclesPer3Frames / 3;
    {
        std::lock_guard audioLock(sAudioMutex);
        sLockedFrames++;
        for (u64 i = 0; i < cycles; ++i) {
            locked_cycle(buffer + i * kCycleFrames * sChannels);
        }
    }
    if (!sDeviceStarted || sBackground.load()) {
        return;
    }
    const int frameBytes = static_cast<int>(kMixRate / 60 * sChannels * sizeof(s16));
    const int queued = SDL_GetAudioStreamQueued(sStream);
    if (queued < 0 || queued > frameBytes * kQueueHigh) {
        return;
    }
    if (queued < frameBytes * kQueueLow) {
        static const s16 silence[kMixRate / 60 * 8] = {};
        for (int n = queued; n < frameBytes * kQueueStart; n += frameBytes) {
            SDL_PutAudioStreamData(sStream, silence, frameBytes);
        }
    }
    SDL_PutAudioStreamData(sStream, buffer, static_cast<int>(cycles * kCycleFrames * sChannels * sizeof(s16)));
}

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
            SDL_PauseAudioStreamDevice(sStream);
            Log.info("Paused for the background");
        }
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        if (sBackground.exchange(false) && sDeviceStarted) {
            SDL_ResumeAudioStreamDevice(sStream);
            Log.info("Resumed in the foreground");
        }
        break;
    default:
        break;
    }
    return true;
}

bool open_audio(SND_PC_CONFIG *obtained) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }
    sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, stream_callback, nullptr);
    if (sStream == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_AudioSpec device{};
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sStream), &device, nullptr)) {
        device.channels = 2;
    }
#if PB_AUDIO_THREADS
    // The speaker layouts MusyX mixes for (as its SDL adapter chose them).
    const u32 channels = device.channels == 4 || device.channels == 6 || device.channels == 8 ? device.channels : 2;
#else
    const u32 channels = 2;
#endif
    SND_PC_CONFIG config{kMixRate, channels};
    const SDL_AudioSpec input{SDL_AUDIO_S16, static_cast<int>(channels), static_cast<int>(kMixRate)};
    if (!SDL_SetAudioStreamFormat(sStream, &input, nullptr) || !sndPCConfigure(&config)) {
        SDL_DestroyAudioStream(sStream);
        sStream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    sChannels = channels;
    *obtained = config;
#if !PB_AUDIO_THREADS
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
#endif
    return true;
}

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
    sDeviceOpened = open_audio(&obtained);
    if (sDeviceOpened) {
        Log.info("Audio output: {} Hz, {} channels", obtained.mixRate, obtained.channels);
    } else {
        Log.warn("Unable to open an audio device, sound will be muted: {}", SDL_GetError());
        // Still mixed at kMixRate (MusyX's default is 32000 Hz): a peer without sound must end sounds when the others do.
        const SND_PC_CONFIG config{kMixRate, 2};
        sndPCConfigure(&config);
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
        sDeviceStarted = true;
        sDeviceStarted = SDL_ResumeAudioStreamDevice(sStream);
        if (sDeviceStarted) {
            if (sBackground.load()) {
                SDL_PauseAudioStreamDevice(sStream);
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
    // netplay_end, or a session that began before this build's MusyX was up: follow netplay.
    const bool locked = partyboard_audio_net_mode() != PB_AUDIO_NET_OFF;
    if (locked != sLocked.load()) {
        partyboard_audio_net_clock_begin(locked);
    }
    if (sLocked.load()) {
        locked_frame();
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
        SDL_DestroyAudioStream(sStream);
        sStream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
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

BOOL partyboard_audio_net_clock_begin(BOOL locked) {
    {
        // The read under way on the thread finishes first (it takes sAudioMutex: not held here).
        std::unique_lock lock(sReadMutex);
        sLocked = locked != FALSE;
        sReadCond.wait(lock, [] { return !sReadBusy; });
    }
    sReadCond.notify_all();
    if (!sndIsInstalled()) {
        return FALSE;
    }
    // Once this lock is ours the device callback is not rendering, and from now on sees sLocked.
    std::lock_guard audioLock(sAudioMutex);
    sClockSet = false;
    if (locked) {
        static s16 scratch[kCycleFrames * 8];
        stream_read_service();
        // The cycle the device was in the middle of: whole cycles from here on.
        const SND_PC_RENDER_INFO info = sndPCGetRenderInfo();
        sndPCRender(scratch, static_cast<size_t>(info.renderedFrames - info.outputFrames));
        Log.info("MusyX on the game's frame clock");
    } else {
        Log.info("MusyX on the device's clock");
    }
    return TRUE;
}

void partyboard_audio_net_clock_settle(void) {
    std::lock_guard audioLock(sAudioMutex);
    for (u32 i = 0; i < kSettleCycles; ++i) {
        locked_cycle(nullptr);
    }
    last_rnd = 1;
    sLockedFrames = 0;
}

void partyboard_audio_net_clock_cycle(void) {
    std::lock_guard audioLock(sAudioMutex);
    locked_cycle(nullptr);
}

BOOL partyboard_audio_read_async(DVDFileInfo *fileInfo, void *addr, s32 length, s32 offset, DVDCallback callback) {
    // DVDGetCommandBlockStatus
    fileInfo->cb.state = DVD_STATE_BUSY;
    {
        std::lock_guard lock(sReadMutex);
        sReadQueue.push_back({fileInfo, addr, length, offset, callback});
    }
    sReadCond.notify_all();
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
