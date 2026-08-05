/*
 * rogue_msg.c -- the message line and its history.
 * ---------------------------------------------------------------------------
 * The 6502 build had no message system. `TMS_Rogue.asm:2492`, on the bag-full
 * case, says so outright: "no message yet -- eventual P10 polish". Row 23 got
 * used for one-shot prompts ("DIRECTION?", "NO DAGGER") and nothing else, so
 * everything the dungeon did to you was something you had to infer from the
 * HP counter moving.
 *
 * This is the missing half of a roguelike: the line that turns a sequence of
 * state changes into a story. A turn can produce several events -- you hit
 * the troll, the troll hits you, the ring pulses -- so messages ACCUMULATE
 * into one line while they fit, and only spill to a new one when they do not.
 * Everything is kept in a ring buffer that the log screen reads back.
 *
 * Constraint inherited from the artwork: the font is uppercase, digits and
 * `+ - . / : ? [ ]`. Nothing here may emit anything else, or it prints blank.
 */

#include "rogue.h"

#include <string.h>

#define MSG_WIDTH  32               /* one name-table row */
#define MSG_HIST   24               /* what the log screen can show */

static char  gLine[MSG_WIDTH + 1];  /* what row 21 shows right now */
static char  gHist[MSG_HIST][MSG_WIDTH + 1];
static short gHistCount;            /* entries written, saturating at MSG_HIST */
static short gHistNext;             /* ring cursor */

static void HistPush(const char *s)
{
    if (!*s)
        return;
    strncpy(gHist[gHistNext], s, MSG_WIDTH);
    gHist[gHistNext][MSG_WIDTH] = '\0';
    gHistNext = (short)((gHistNext + 1) % MSG_HIST);
    if (gHistCount < MSG_HIST)
        gHistCount++;
}

/* Start of a player action: the line is cleared so the turn's own events are
 * the only thing on it. The history keeps the previous turn. */
void MsgNewTurn(void)
{
    gLine[0] = '\0';
}

void MsgReset(void)
{
    gLine[0] = '\0';
    gHistCount = 0;
    gHistNext = 0;
}

/* Append if the line still has room, otherwise start a fresh one. Either way
 * the message lands in the history intact -- the log never loses an event to
 * a full row. */
void Msg(const char *s)
{
    size_t have, want;
    if (!s || !*s)
        return;
    HistPush(s);

    have = strlen(gLine);
    want = strlen(s);
    if (have == 0) {
        strncpy(gLine, s, MSG_WIDTH);
        gLine[MSG_WIDTH] = '\0';
        return;
    }
    if (have + 1 + want <= MSG_WIDTH) {
        gLine[have] = ' ';
        strncpy(gLine + have + 1, s, MSG_WIDTH - have - 1);
        gLine[MSG_WIDTH] = '\0';
        return;
    }
    strncpy(gLine, s, MSG_WIDTH);       /* no room: the newest wins the row */
    gLine[MSG_WIDTH] = '\0';
}

/* "A B" without pulling in printf: the two calls the game actually needs. */
void Msg2(const char *a, const char *b)
{
    char buf[MSG_WIDTH + 1];
    size_t n = 0, k;
    if (!a) a = "";
    if (!b) b = "";
    for (k = 0; a[k] && n < MSG_WIDTH; k++) buf[n++] = a[k];
    if (n < MSG_WIDTH) buf[n++] = ' ';
    for (k = 0; b[k] && n < MSG_WIDTH; k++) buf[n++] = b[k];
    buf[n] = '\0';
    Msg(buf);
}

void MsgNum(const char *a, u8 v)
{
    char buf[MSG_WIDTH + 1];
    size_t n = 0, k;
    if (!a) a = "";
    for (k = 0; a[k] && n < MSG_WIDTH; k++) buf[n++] = a[k];
    if (n < MSG_WIDTH) buf[n++] = ' ';
    if (v >= 100 && n < MSG_WIDTH) buf[n++] = (char)('0' + v / 100);
    if (v >= 10  && n < MSG_WIDTH) buf[n++] = (char)('0' + (v / 10) % 10);
    if (n < MSG_WIDTH)             buf[n++] = (char)('0' + v % 10);
    buf[n] = '\0';
    Msg(buf);
}

const char *MsgLine(void)
{
    return gLine;
}

short MsgHistCount(void)
{
    return gHistCount;
}

/* i = 0 is the most recent. */
const char *MsgHist(short i)
{
    short slot;
    if (i < 0 || i >= gHistCount)
        return "";
    slot = (short)((gHistNext - 1 - i + MSG_HIST * 2) % MSG_HIST);
    return gHist[slot];
}
