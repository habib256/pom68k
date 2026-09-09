// POM68K — asynchronous SCC byte-wire seam for host serial transports.

#include "Scc8530.h"

bool Scc8530::canInjectRxByte(int ch) const {
    const Chan& c = ch_[ch & 1];
    return !sdlcMode(c) && rxEnabled(c) && c.fifo.size() < 3;
}

void Scc8530::emitAsyncByte(int ch, const Chan& c) {
    // Internal loopback disconnects TxD from the external pin.
    if (!sdlcMode(c) && !(c.wr[14] & 0x10) && onTxByte)
        onTxByte(ch, c.txShiftData);
}
