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
 */
#define NET_SE_HANDLES 256
#define NET_STREAM_HANDLES 64
#define NET_MIDI_CTRL 12

typedef struct {
    s32 net;
    s32 real;
} NetHandle;

/* Debug: PB_AUDIO_REAL_STATUS=1 gives a --script run msm's real answers, to play offline's path from a script. */
static BOOL NetAnswers(void)
{
    static int real = -1;

    if (!netplay_active()) {
        return FALSE;
    }
    if (real < 0) {
        const char *env = getenv("PB_AUDIO_REAL_STATUS");
        real = env != NULL && env[0] == '1';
    }
    return !real;
}

static NetHandle netSe[NET_SE_HANDLES];
static NetHandle netStream[NET_STREAM_HANDLES];
static s32 netSeNext;
static s32 netStreamNext;

void partyboard_audio_net_reset(void)
{
    msmSysIrqDisable();
    memset(netSe, 0xFF, sizeof(netSe));
    memset(netStream, 0xFF, sizeof(netStream));
    netSeNext = netStreamNext = 0;
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
        return NetAnswers() ? (net) : result;     \
    }

MSM_LOCKED_NET(s32, msmMusStop, (int musNo, s32 speed), (musNo, speed), 0)
MSM_LOCKED_NET(s32, msmMusPause, (int musNo, BOOL pause, s32 speed), (musNo, pause, speed), 0)
MSM_LOCKED_NET(s32, msmMusSetParam, (s32 musNo, MSM_MUSPARAM *param), (musNo, param), 0)
MSM_LOCKED_NET(int, msmMusPlay, (int musId, MSM_MUSPARAM *musParam), (musId, musParam),
    (musParam != NULL && (musParam->flag & MSM_MUSPARAM_CHAN)) ? musParam->chan : 0)

s32 msmMusGetStatusUnlocked(int musNo);
s32 msmMusGetStatus(int musNo)
{
    return NetAnswers() ? 0 : msmMusGetStatusUnlocked(musNo);
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
    if (NetAnswers()) {
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
    if (NetAnswers()) {
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
    if (NetAnswers()) {
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
    result = msmSeGetStatusUnlocked(seNo);
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
    if (!NetAnswers()) {
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
    if (NetAnswers()) {
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
    if (NetAnswers()) {
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
    result = msmStreamGetStatusUnlocked(streamNo);
    msmSysIrqEnable();
    return result;
}
