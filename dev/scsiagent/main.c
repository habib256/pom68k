/* main.c — « POM68K Disques », the guest-side agent (docs/SCSI_HOTPLUG.md § 3).
 *
 * A small application that polls the emulator twice a second through the
 * vendor SCSI command $C0 on target 0 (src/ScsiAgentMailbox.h), performs a
 * mount or unmount with the File Manager (mount.c) and answers with $C1.
 * Every poll is the emulator's heartbeat for « agent présent ». Quitting
 * unmounts the volumes served by our driver first: the driver's code
 * lives in this application. */
#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <AppleEvents.h>
#include <OSUtils.h>
#include <Errors.h>
#include <string.h>
#include <stdio.h>

#include "drv.h"
#include "glue.h"
#include "mount.h"


#define kPollTicks 30
#define kPayload   64

static WindowPtr         gWindow;
static AgentDriverState *gDriver;
static char              gLine1[80] = "POM68K Disques - agent de montage";
static char              gLine2[80] = "en attente du pilote";
static char              gLine3[80] = "";
static unsigned long     gPolls, gErrors;
static OSErr             gDriverErr;

static void redraw(void)
{
    Rect r = gWindow->portRect;
    SetPort(gWindow);
    EraseRect(&r);
    MoveTo(10, 20); DrawText(gLine1, 0, (short)strlen(gLine1));
    MoveTo(10, 40); DrawText(gLine2, 0, (short)strlen(gLine2));
    MoveTo(10, 60); DrawText(gLine3, 0, (short)strlen(gLine3));
}

static void setLine(char *line, const char *text)
{
    strncpy(line, text, 79);
    line[79] = 0;
    redraw();
}

/* One poll: ask, act, answer. */
static void poll(void)
{
    unsigned char buf[kPayload];
    unsigned char cdb[6] = { 0xC0, 0, 0, 0, kPayload, 0 };
    unsigned char kind, id, seq;
    short         drive = 0;
    unsigned char volName[28];
    OSErr         e;
    char          msg[80];

    memset(buf, 0, sizeof buf);
    e = agentScsiTransfer(0, cdb, 6, (Ptr)buf, kPayload, false, 0, 0);
    if (e != noErr || memcmp(buf, "POMA", 4) != 0) {
        gErrors++;
        sprintf(msg, "sondage : erreur %d (%lu)", e, gErrors);
        setLine(gLine3, msg);
        return;
    }
    gPolls++;
    kind = buf[5]; id = buf[6]; seq = buf[7];
    if (kind == 0) {
        if ((gPolls & 7) == 0) {
            sprintf(msg, "sondages : %lu", gPolls);
            setLine(gLine3, msg);
        }
        return;
    }
    volName[0] = 0;
    if (!gDriver) e = gDriverErr ? gDriverErr : unitEmptyErr;   /* no driver: say so */
    else if (kind == 1) e = agentMount(gDriver, (short)id, &drive, volName);
    else if (kind == 2) e = agentUnmount(gDriver, (short)id);
    else e = paramErr;

    memset(buf, 0, sizeof buf);
    memcpy(buf, "POMR", 4);
    buf[4] = 1; buf[5] = kind; buf[6] = id; buf[7] = seq;
    buf[8] = (unsigned char)(((unsigned short)e) >> 8); buf[9] = (unsigned char)e;
    buf[10] = (unsigned char)(((unsigned short)drive) >> 8); buf[11] = (unsigned char)drive;
    if (e == noErr && kind == 1) {
        memcpy(buf + 12, volName, (size_t)volName[0] + 1);
    } else if (e == noErr) {
        memcpy(buf + 12, "\pdemonte", 8);       /* ASCII: MacRoman is not UTF-8 */
    } else {
        sprintf(msg, "erreur %d", e);
        buf[12] = (unsigned char)strlen(msg);
        memcpy(buf + 13, msg, buf[12]);
    }
    cdb[0] = 0xC1;
    agentScsiTransfer(0, cdb, 6, (Ptr)buf, kPayload, true, 0, 0);

    sprintf(msg, "%s SCSI %d : %s %.*s", kind == 1 ? "montage" : "demontage", id,
            e == noErr ? "ok" : "erreur", (int)buf[12], (const char *)buf + 13);
    setLine(gLine2, msg);
}

int main(void)
{
    Rect          bounds = { 60, 40, 140, 420 };
    EventRecord   ev;
    unsigned long nextPoll = TickCount();
    Boolean       running = true;
    OSErr         e;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    gWindow = NewWindow(NULL, &bounds, "\pPOM68K Disques", true, noGrowDocProc,
                        (WindowPtr)-1, true, 0);
    SetPort(gWindow);

    e = agentDriverInstall(&gDriver);
    if (e != noErr) {
        char msg[80];
        gDriverErr = e;
        sprintf(msg, "pilote refuse : erreur %d, TIB %d octets", e, (int)sizeof(SCSIInstr));
        setLine(gLine2, msg);
    } else {
        char msg[80];
        sprintf(msg, "pilote .POM68KHD pret (une unite SCSI par cible montee)");
        setLine(gLine2, msg);
    }

    while (running) {
        if (gDriver) gDriver->heartbeat++;
        /* Not diskMask: the disk-inserted event PBMountVol posts is the
         * Finder's to consume (it learns of the new volume from it); the
         * front application swallowing it left the Finder with a volume it
         * never heard of — « There is a problem with the disk » (2026-09-14). */
        if (WaitNextEvent(everyEvent & ~diskMask, &ev, kPollTicks / 2, NULL)) {
            switch (ev.what) {
                case keyDown:
                    if ((ev.modifiers & cmdKey) && (ev.message & charCodeMask) == 'q')
                        running = false;
                    break;
                case mouseDown: {
                    WindowPtr w;
                    short part = FindWindow(ev.where, &w);
                    if (part == inGoAway && w == gWindow &&
                        TrackGoAway(w, ev.where)) running = false;
                    else if (part == inDrag && w == gWindow)
                        DragWindow(w, ev.where, &qd.screenBits.bounds);
                    break;
                }
                case updateEvt:
                    BeginUpdate(gWindow);
                    redraw();
                    EndUpdate(gWindow);
                    break;
                case kHighLevelEvent:
                    AEProcessAppleEvent(&ev);    /* replies to our own sends */
                    break;
                default:
                    break;
            }
        }
        if (TickCount() >= nextPoll) {
            nextPoll = TickCount() + kPollTicks;
            poll();
        }
    }
    if (gDriver) {
        agentUnmountAll(gDriver);
        agentDriverRemove(gDriver);
    }
    return 0;
}
