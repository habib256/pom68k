/*
 * snd.c -- Sound Manager square-wave effects. See snd.h for the contract.
 * ---------------------------------------------------------------------------
 * Mechanics. One channel on noteSynth (the classic square-wave voice, the
 * only synth every Sound Manager machine has). An effect is a table of
 * steps; each step becomes ONE queued SndCommand, so the whole effect costs
 * a handful of SndDoCommand calls and zero wait states -- the channel's own
 * 128-deep queue is the sequencer. `freqDurationCmd` (named noteCmd in the
 * multiversal headers) takes the duration in HALF-milliseconds in param1
 * and a MIDI note number in param2 (middle C = 60) -- Inside Macintosh VI,
 * "Sound Manager", table 22-2.
 *
 * Timbre is the whole aesthetic: ~20 is a clear, almost-sine chime for good
 * news (pickups, healing), ~200 is the buzzy edge for violence. The timbre
 * command must precede the notes it colours; a flush resets nothing, so
 * every effect states its own timbre and amplitude up front.
 */

#include <Sound.h>

#include "snd.h"

/* --- Effect scores --------------------------------------------------------
 * op: 'N' note (a = MIDI note, b = duration ms), 'R' rest (b = ms),
 *     'A' amplitude 0-255 (a), 'T' timbre 0-255 (a). 0 terminates.
 * Durations stay in ms here (max 255) and are doubled into half-ms at play
 * time; nothing musical in this game needs to ring longer than 250 ms.
 */
typedef struct { unsigned char op, a, b; } SfxStep;

/* MIDI helpers, so the scores below read like music and not like numbers. */
#define C3 48
#define E3 52
#define F3 53
#define G3 55
#define B2 47
#define G2 43
#define C4 60
#define D4 62
#define E4 64
#define F4 65
#define G4 67
#define A4 69
#define C5 72
#define D5 74
#define E5 76
#define G5 79
#define A5 81
#define C6 84

static const SfxStep kTitle[] = {          /* a small curtain-raiser */
    {'T', 40, 0}, {'A', 200, 0},
    {'N', C4, 50}, {'N', E4, 50}, {'N', G4, 50}, {'N', C5, 120}, {0, 0, 0} };

static const SfxStep kStairs[] = {         /* down, down, down */
    {'T', 90, 0}, {'A', 190, 0},
    {'N', C5, 35}, {'N', A4, 35}, {'N', F4, 35}, {'N', D4, 35},
    {'N', C4, 70}, {0, 0, 0} };

static const SfxStep kHit[] = {            /* short, mean */
    {'T', 190, 0}, {'A', 220, 0},
    {'N', A4, 25}, {'N', E4, 35}, {0, 0, 0} };

static const SfxStep kKill[] = {           /* the hit, resolved downward */
    {'T', 190, 0}, {'A', 230, 0},
    {'N', C5, 30}, {'N', G4, 30}, {'N', E4, 30}, {'N', C4, 60}, {0, 0, 0} };

static const SfxStep kHurt[] = {           /* low and unpleasant */
    {'T', 220, 0}, {'A', 240, 0},
    {'N', E3, 40}, {'N', C3, 60}, {0, 0, 0} };

static const SfxStep kHurtLow[] = {        /* same, plus a two-tone alarm */
    {'T', 220, 0}, {'A', 240, 0},
    {'N', E3, 40}, {'N', C3, 60}, {'R', 0, 60},
    {'T', 60, 0}, {'A', 210, 0},
    {'N', E4, 70}, {'N', C4, 70}, {'N', E4, 70}, {'N', C4, 70}, {0, 0, 0} };

static const SfxStep kGlance[] = {         /* the tunic shrugs it off */
    {'T', 140, 0}, {'A', 110, 0},
    {'N', G3, 25}, {0, 0, 0} };

static const SfxStep kBossHit[] = {        /* two floors below kHurt */
    {'T', 240, 0}, {'A', 250, 0},
    {'N', C3, 55}, {'N', G2, 90}, {0, 0, 0} };

static const SfxStep kPickup[] = {         /* the classic bright up-chirp */
    {'T', 20, 0}, {'A', 180, 0},
    {'N', C5, 20}, {'N', E5, 20}, {'N', G5, 35}, {0, 0, 0} };

static const SfxStep kQuaff[] = {          /* three swallows, ending high */
    {'T', 30, 0}, {'A', 190, 0},
    {'N', G4, 30}, {'N', C5, 30}, {'N', E5, 50}, {0, 0, 0} };

static const SfxStep kScroll[] = {         /* dry, papery, a little odd */
    {'T', 100, 0}, {'A', 150, 0},
    {'N', D5, 25}, {'N', D4, 45}, {0, 0, 0} };

static const SfxStep kBuff[] = {           /* something clicks into place */
    {'T', 60, 0}, {'A', 200, 0},
    {'N', C4, 25}, {'N', G4, 25}, {'N', C5, 45}, {0, 0, 0} };

static const SfxStep kThrow[] = {          /* the dagger's flight, falling */
    {'T', 200, 0}, {'A', 160, 0},
    {'N', C6, 15}, {'N', A5, 15}, {'N', F4 + 12, 15}, {'N', D5, 15}, {0, 0, 0} };

static const SfxStep kPit[] = {            /* floor gone, then impact */
    {'T', 230, 0}, {'A', 240, 0},
    {'N', C3, 80}, {'R', 0, 25}, {'N', B2, 120}, {0, 0, 0} };

static const SfxStep kLevelUp[] = {        /* four rising steps */
    {'T', 50, 0}, {'A', 210, 0},
    {'N', C4, 30}, {'N', D4, 30}, {'N', E4, 30}, {'N', G4, 60}, {0, 0, 0} };

static const SfxStep kDeath[] = {          /* the long way down */
    {'T', 210, 0}, {'A', 240, 0},
    {'N', G3, 90}, {'N', E3, 90}, {'N', C3, 90}, {'N', G2, 200}, {0, 0, 0} };

static const SfxStep kWin[] = {            /* earned */
    {'T', 50, 0}, {'A', 230, 0},
    {'N', C4, 60}, {'N', C4, 30}, {'N', C4, 30}, {'N', E4, 60},
    {'N', G4, 60}, {'N', C5, 160}, {0, 0, 0} };

static const SfxStep *const kScore[SFX_COUNT] = {
    0,          /* SFX_NONE */
    kTitle, kStairs, kHit, kKill, kHurt, kHurtLow, kGlance, kBossHit,
    kPickup, kQuaff, kScroll, kBuff, kThrow, kPit, kLevelUp, kDeath, kWin,
};

/* --- Channel ------------------------------------------------------------- */

static SndChannelPtr gChan;     /* 0 = unavailable, stay silent */
static int gOn = 1;

void SfxInit(void)
{
    /* SndNewChannel is System 6.0.4+; older Systems leave the trap
     * unimplemented and the low-memory version check is the cheap,
     * era-standard guard (the game itself runs down to 4.1). */
    if (LMGetSysVersion() < 0x0604)
        return;
    if (SndNewChannel(&gChan, noteSynth, 0, 0) != noErr)
        gChan = 0;
}

void SfxShutdown(void)
{
    if (gChan) {
        SndDisposeChannel(gChan, true);    /* true = quiet NOW */
        gChan = 0;
    }
}

int SfxAvailable(void) { return gChan != 0; }
int SfxEnabled(void)   { return gChan != 0 && gOn; }

void SfxSetEnabled(int on)
{
    gOn = on ? 1 : 0;
    if (!gOn && gChan) {
        SndCommand c;
        c.cmd = flushCmd;  c.param1 = 0; c.param2 = 0;
        SndDoImmediate(gChan, &c);
        c.cmd = quietCmd;
        SndDoImmediate(gChan, &c);
    }
}

void SfxPlay(short id)
{
    const SfxStep *s;
    SndCommand c;

    if (!gChan || !gOn || id <= SFX_NONE || id >= SFX_COUNT)
        return;
    s = kScore[id];
    if (!s)
        return;

    /* Last event wins: drop whatever the previous effect still had queued,
     * then stop the tone that is sounding right now. */
    c.cmd = flushCmd;  c.param1 = 0; c.param2 = 0;
    SndDoImmediate(gChan, &c);
    c.cmd = quietCmd;
    SndDoImmediate(gChan, &c);

    for (; s->op; s++) {
        switch (s->op) {
        case 'N':
            c.cmd = noteCmd;               /* Apple's freqDurationCmd */
            c.param1 = (short)(s->b * 2);  /* ms -> half-ms */
            c.param2 = s->a;               /* MIDI note */
            break;
        case 'R':
            c.cmd = restCmd;
            c.param1 = (short)(s->b * 2);
            c.param2 = 0;
            break;
        case 'A':
            c.cmd = ampCmd;
            c.param1 = s->a;
            c.param2 = 0;
            break;
        case 'T':
            c.cmd = timbreCmd;
            c.param1 = s->a;
            c.param2 = 0;
            break;
        default:
            return;
        }
        /* noWait = the call queues and returns; the channel sequences. */
        if (SndDoCommand(gChan, &c, true) != noErr)
            return;                        /* queue full -- drop the tail */
    }
}
