// see include/msm/msmpc.h
#define MSM_PC_WRAPPERS
#include "game/msm.h"
#include "msm/msmsys.h"
#include "port/audio.h"
#include "port/netplay.h"

#include <stdlib.h>
#include <string.h>

/*
 * N112: what the game reads back from msm while netplay_active() (an online match, and every --script/--hash-log
 * run) must not come from the audio thread: a sound's status, a stream's or a song's, and which handle a play
 * got, all move with the 5 ms MusyX cycles the device pulls, so two peers would read different values on the
 * same frame. Offline the game gets msm's real answers. During netplay it gets what the port's stubs gave it
 * before audio existed (N85: every sound, song and stream reads 0, "done"; HuAudSeqMidiCtrlGet's 12), and
 * handles from a counter of plays: every play "succeeds" with the next number, whether or not msm found a free
 * player or voice. Each number maps to msm's real handle, so stops, fades and parameter changes still reach the
 * sound that is playing. Songs keep their channel (msmMusPlay returns the channel it played on, a value of the
 * call, not of time).
 *
 * N122: during netplay MusyX runs on a clock of game frames instead (src/port/audio.cpp: 10 cycles of 5 ms every 3
 * frames, on the game thread, PCM queued to the device), so msm's real statuses are the same on every peer: board
 * music pauses for events (status 3), w04's coin event follows its song's MIDI controller, jingle waits wait. The
 * handle counter stays (a handle number is not a value of msm's history before the match); a status read maps it to
 * msm's handle. Debug: PB_AUDIO_NET_STUBS=1 gives netplay N112's answers again (0 and 12, on the same clock).
 */
#define NET_SE_HANDLES 256
#define NET_STREAM_HANDLES 64
#define NET_MIDI_CTRL 12

typedef struct {
    s32 net;
    s32 real;
} NetHandle;

static BOOL EnvSet(const char *name)
{
    const char *env = getenv(name);
    return env != NULL && env[0] == '1';
}

/* Debug: PB_AUDIO_REAL_STATUS=1 gives a --script run msm's real answers on the device's clock, to play offline's path
 * from a script. */
s32 partyboard_audio_net_mode(void)
{
    static s32 mode = -1;

    if (!netplay_active()) {
        return PB_AUDIO_NET_OFF;
    }
    if (mode < 0) {
        mode = EnvSet("PB_AUDIO_REAL_STATUS") ? PB_AUDIO_NET_OFF
            : EnvSet("PB_AUDIO_NET_STUBS")    ? PB_AUDIO_NET_STUBS
                                              : PB_AUDIO_NET_LOCKED;
    }
    return mode;
}

/* N112's stub answers (PB_AUDIO_NET_STUBS). */
static BOOL NetAnswers(void)
{
    return partyboard_audio_net_mode() == PB_AUDIO_NET_STUBS;
}

/* Handles from the counter: every netplay mode but PB_AUDIO_REAL_STATUS. */
static BOOL NetHandles(void)
{
    return partyboard_audio_net_mode() != PB_AUDIO_NET_OFF;
}

static s32 netNumPlayPolls;
static NetHandle netSe[NET_SE_HANDLES];
static NetHandle netStream[NET_STREAM_HANDLES];
static s32 netSeNext;
static s32 netStreamNext;

void partyboard_audio_net_reset(void)
{
    /* N122: every peer starts the match's clock from the same state: nothing playing, MusyX's random numbers from 1.
     * Outside msm's lock: clock_begin waits for the stream read under way, which takes it. */
    BOOL locked = partyboard_audio_net_mode() != PB_AUDIO_NET_OFF;
    BOOL running = partyboard_audio_net_clock_begin(locked);

    msmSysIrqDisable();
    if (running && locked) {
        msmSeStopAll(FALSE, 0);
        msmMusStopAll(FALSE, 0);
        msmStreamStopAll(0);
        partyboard_audio_net_clock_settle();
    }
    memset(netSe, 0xFF, sizeof(netSe));
    memset(netStream, 0xFF, sizeof(netStream));
    netSeNext = netStreamNext = 0;
    netNumPlayPolls = 0;
    msmSysIrqEnable();
}

static s32 NetHandleAdd(NetHandle *tbl, s32 num, s32 *next, s32 real)
{
    s32 net = *next;

    *next = (net + 1) & 0x7FFFFFFF;
    tbl[net % num].net = net;
    tbl[net % num].real = real;
    return net;
}

/* -1 (no sound) once a newer play has taken the slot, or for a handle from before the match. */
static s32 NetHandleReal(const NetHandle *tbl, s32 num, s32 net)
{
    if (net < 0 || tbl[net % num].net != net) {
        return -1;
    }
    return tbl[net % num].real;
}

#define MSM_LOCKED(type, name, params, args) \
    type name##Unlocked params;              \
    type name params                         \
    {                                        \
        type result;                         \
        msmSysIrqDisable();                  \
        result = name##Unlocked args;        \
        msmSysIrqEnable();                   \
        return result;                       \
    }

#define MSM_LOCKED_VOID(name, params, args) \
    void name##Unlocked params;             \
    void name params                        \
    {                                       \
        msmSysIrqDisable();                 \
        name##Unlocked args;                \
        msmSysIrqEnable();                  \
    }

MSM_LOCKED(BOOL, msmSysSetOutputMode, (SND_OUTPUTMODE mode), (mode))
MSM_LOCKED(s32, msmSysSetAux, (s32 auxA, s32 auxB), (auxA, auxB))

MSM_LOCKED_VOID(msmSeStopAll, (BOOL checkGrp, s32 speed), (checkGrp, speed))
MSM_LOCKED_VOID(msmSePauseAll, (BOOL pause, s32 speed), (pause, speed))
MSM_LOCKED(s32, msmSeSetListener, (Vec *pos, Vec *heading, float sndDist, float sndSpeed, MSM_SELISTENER *listener),
    (pos, heading, sndDist, sndSpeed, listener))
MSM_LOCKED(s32, msmSeUpdataListener, (Vec *pos, Vec *heading), (pos, heading))
MSM_LOCKED_VOID(msmSeDelListener, (void), ())

MSM_LOCKED_VOID(msmMusStopAll, (BOOL checkGrp, s32 speed), (checkGrp, speed))
MSM_LOCKED_VOID(msmMusPauseAll, (BOOL pause, s32 speed), (pause, speed))

MSM_LOCKED_VOID(msmStreamStopAll, (s32 speed), (speed))
MSM_LOCKED_VOID(msmStreamSetMasterVolume, (s32 vol), (vol))

/* Locked, and during netplay its answer is `net` (computed from the call's arguments) instead of msm's. */
#define MSM_LOCKED_NET(type, name, params, args, net) \
    type name##Unlocked params;                       \
    type name params                                  \
    {                                                 \
        type result;                                  \
        msmSysIrqDisable();                           \
        result = name##Unlocked args;                 \
        msmSysIrqEnable();                            \
        return NetHandles() ? (net) : result;     \
    }

MSM_LOCKED_NET(s32, msmMusStop, (int musNo, s32 speed), (musNo, speed), 0)
MSM_LOCKED_NET(s32, msmMusPause, (int musNo, BOOL pause, s32 speed), (musNo, pause, speed), 0)
MSM_LOCKED_NET(s32, msmMusSetParam, (s32 musNo, MSM_MUSPARAM *param), (musNo, param), 0)
MSM_LOCKED_NET(int, msmMusPlay, (int musId, MSM_MUSPARAM *musParam), (musId, musParam),
    (musParam != NULL && (musParam->flag & MSM_MUSPARAM_CHAN)) ? musParam->chan : 0)

s32 msmMusGetStatusUnlocked(int musNo);
s32 msmMusGetStatus(int musNo)
{
    s32 result;

    if (NetAnswers()) {
        return 0;
    }
    msmSysIrqDisable();
    result = msmMusGetStatusUnlocked(musNo);
    msmSysIrqEnable();
    return result;
}

s32 msmMusGetMidiCtrlUnlocked(int musNo, s32 channel, s32 ctrl);
s32 msmMusGetMidiCtrl(int musNo, s32 channel, s32 ctrl)
{
    s32 result;

    if (NetAnswers()) {
        return NET_MIDI_CTRL;
    }
    msmSysIrqDisable();
    result = msmMusGetMidiCtrlUnlocked(musNo, channel, ctrl);
    msmSysIrqEnable();
    return result;
}

int msmSePlayUnlocked(int seId, MSM_SEPARAM *param);
int msmSePlay(int seId, MSM_SEPARAM *param)
{
    int result;

    msmSysIrqDisable();
    result = msmSePlayUnlocked(seId, param);
    if (NetHandles()) {
        result = NetHandleAdd(netSe, NET_SE_HANDLES, &netSeNext, result);
    }
    msmSysIrqEnable();
    return result;
}

s32 msmSeStopUnlocked(int seNo, s32 speed);
s32 msmSeStop(int seNo, s32 speed)
{
    s32 result;

    msmSysIrqDisable();
    if (NetHandles()) {
        msmSeStopUnlocked(NetHandleReal(netSe, NET_SE_HANDLES, seNo), speed);
        result = 0;
    } else {
        result = msmSeStopUnlocked(seNo, speed);
    }
    msmSysIrqEnable();
    return result;
}

s32 msmSeSetParamUnlocked(int seNo, MSM_SEPARAM *param);
s32 msmSeSetParam(int seNo, MSM_SEPARAM *param)
{
    s32 result;

    msmSysIrqDisable();
    if (NetHandles()) {
        msmSeSetParamUnlocked(NetHandleReal(netSe, NET_SE_HANDLES, seNo), param);
        result = 0;
    } else {
        result = msmSeSetParamUnlocked(seNo, param);
    }
    msmSysIrqEnable();
    return result;
}

s32 msmSeGetStatusUnlocked(int seNo);
s32 msmSeGetStatus(int seNo)
{
    s32 result;

    if (NetAnswers()) {
        return 0;
    }
    msmSysIrqDisable();
    result = msmSeGetStatusUnlocked(NetHandles() ? NetHandleReal(netSe, NET_SE_HANDLES, seNo) : seNo);
    msmSysIrqEnable();
    return result;
}

/* During netplay: the handles of the plays still mapped (CharFXStop stops them; nothing keeps the count). */
s32 msmSeGetEntryIDUnlocked(s32 seId, int *seNo);
s32 msmSeGetEntryID(s32 seId, int *seNo)
{
    int real[MSM_ENTRY_SENO_MAX];
    s32 num;
    s32 i;
    s32 j;
    s32 n;

    msmSysIrqDisable();
    if (!NetHandles()) {
        num = msmSeGetEntryIDUnlocked(seId, seNo);
        msmSysIrqEnable();
        return num;
    }
    n = msmSeGetEntryIDUnlocked(seId, real);
    num = 0;
    for (i = 0; i < n && i < MSM_ENTRY_SENO_MAX; i++) {
        for (j = 0; j < NET_SE_HANDLES; j++) {
            if (netSe[j].net >= 0 && netSe[j].real == real[i]) {
                if (seNo != NULL) {
                    seNo[num] = netSe[j].net;
                }
                num++;
                break;
            }
        }
    }
    msmSysIrqEnable();
    return num;
}

int msmStreamPlayUnlocked(int streamId, MSM_STREAMPARAM *streamParam);
int msmStreamPlay(int streamId, MSM_STREAMPARAM *streamParam)
{
    int result;

    msmSysIrqDisable();
    result = msmStreamPlayUnlocked(streamId, streamParam);
    if (NetHandles()) {
        result = NetHandleAdd(netStream, NET_STREAM_HANDLES, &netStreamNext, result);
    }
    msmSysIrqEnable();
    return result;
}

s32 msmStreamStopUnlocked(int streamNo, s32 speed);
s32 msmStreamStop(int streamNo, s32 speed)
{
    s32 result;

    msmSysIrqDisable();
    if (NetHandles()) {
        msmStreamStopUnlocked(NetHandleReal(netStream, NET_STREAM_HANDLES, streamNo), speed);
        result = 0;
    } else {
        result = msmStreamStopUnlocked(streamNo, speed);
    }
    msmSysIrqEnable();
    return result;
}

s32 msmStreamGetStatusUnlocked(int streamNo);
s32 msmStreamGetStatus(int streamNo)
{
    s32 result;

    if (NetAnswers()) {
        return 0;
    }
    msmSysIrqDisable();
    result = NetHandles() ? NetHandleReal(netStream, NET_STREAM_HANDLES, streamNo) : streamNo;
    /* A play msm refused, or one older than the 64 kept: done. */
    result = result < 0 && NetHandles() ? 0 : msmStreamGetStatusUnlocked(result);
    msmSysIrqEnable();
    return result;
}

/*
 * N122: SNDGRP_WAIT (src/game/audio.c) polls these after stopping everything, until both read 0 or 500 ms of OSGetTick
 * passed. On a frame clock nothing would end inside that loop, so during netplay each poll that reads sounds still
 * playing lets one 5 ms cycle pass (as the DSP would while the game polled), and after 100 of them in a row (the 500
 * ms) it reads 0. Both depend only on msm's state, not on time.
 */
#define NET_NUMPLAY_POLLS 100

static s32 NetNumPlay(s32 result)
{
    if (result == 0 || partyboard_audio_net_mode() == PB_AUDIO_NET_OFF) {
        netNumPlayPolls = 0;
        return result;
    }
    if (++netNumPlayPolls > NET_NUMPLAY_POLLS) {
        return 0;
    }
    partyboard_audio_net_clock_cycle();
    return result;
}

s32 msmSeGetNumPlayUnlocked(BOOL baseGrp);
s32 msmSeGetNumPlay(BOOL baseGrp)
{
    s32 result;

    msmSysIrqDisable();
    result = NetNumPlay(msmSeGetNumPlayUnlocked(baseGrp));
    msmSysIrqEnable();
    return result;
}

s32 msmMusGetNumPlayUnlocked(BOOL baseGrp);
s32 msmMusGetNumPlay(BOOL baseGrp)
{
    s32 result;

    msmSysIrqDisable();
    result = NetNumPlay(msmMusGetNumPlayUnlocked(baseGrp));
    msmSysIrqEnable();
    return result;
}
