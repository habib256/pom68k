/* POM68K Série — the guest half of `q605_serial_etalon`.
 *
 * A Serial Driver client on the MODEM port (.AIn/.AOut, SCC channel A),
 * started from Startup Items: it announces itself, receives a burst far
 * larger than the SCC's three-byte FIFO and the driver's default 64-byte
 * buffer, and answers with what it got, so the host can check that every
 * byte crossed the SCC, in order, under backpressure.
 *
 *   guest -> host   "POM68K SERIE PRET\r"
 *   host  -> guest  kPayload bytes
 *   guest -> host   "RECU <count> <sum>\r"  then  "FIN\r", and quits
 *
 * <sum> is the 32-bit sum of (byte * (index + 1)): order-sensitive, so a
 * swapped or repeated byte cannot pass. Inside Macintosh: Devices, "Serial
 * Driver" (SerReset, SerSetBuf, SerGetBuf; PBRead/PBWrite on the refnums).
 */
#include <Devices.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <Serial.h>
#include <TextEdit.h>
#include <Windows.h>

#include <stdio.h>
#include <string.h>

enum { kPayload = 6000, kTimeoutTicks = 60L * 60 };

static char gInBuffer[8192];

static OSErr send(short out, const char* text) {
    long count = (long)strlen(text);
    return FSWrite(out, &count, text);
}

int main(void) {
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    short in = 0, out = 0;
    if (OpenDriver("\p.AOut", &out) != noErr || OpenDriver("\p.AIn", &in) != noErr)
        return 1;
    const short config = baud9600 + data8 + stop10 + noParity;
    SerReset(out, config);
    SerReset(in, config);
    SerSetBuf(in, gInBuffer, sizeof gInBuffer);

    send(out, "POM68K SERIE PRET\r");

    unsigned long sum = 0;
    long got = 0;
    const unsigned long deadline = TickCount() + kTimeoutTicks;
    while (got < kPayload && TickCount() < deadline) {
        long ready = 0;
        SerGetBuf(in, &ready);
        if (ready > 0) {
            unsigned char chunk[256];
            long want = ready > (long)sizeof chunk ? (long)sizeof chunk : ready;
            if (want > kPayload - got) want = kPayload - got;
            if (FSRead(in, &want, chunk) != noErr) break;
            for (long i = 0; i < want; ++i)
                sum += (unsigned long)chunk[i] * (unsigned long)(got + i + 1);
            got += want;
        } else {
            EventRecord event;
            WaitNextEvent(everyEvent, &event, 1, NULL);  /* yield to the Finder */
        }
    }

    char reply[64];
    sprintf(reply, "RECU %ld %lu\r", got, sum);
    send(out, reply);
    send(out, "FIN\r");
    /* Let the driver drain its output before the port closes. */
    const unsigned long drain = TickCount() + 60;
    while (TickCount() < drain) {
        EventRecord event;
        WaitNextEvent(everyEvent, &event, 1, NULL);
    }
    CloseDriver(in);
    CloseDriver(out);
    return 0;
}
