#!/usr/bin/env python3
# POM68K — AppleTalk router joining the emulator's LToUDP cable to the
# host-side EtherTalk segment where netatalk (atalkd/afpd) lives. Uses the
# vendored TashRouter (extern/tashrouter). Run AFTER appleshare_bridge.sh
# (which creates pomtap0 and starts atalkd/afpd):
#
#   .venv-tools/bin/python tools/netatalk2/router.py
#
# Zones: "POM68K" on both sides (must match atalkd.conf's -zone).

import logging
import pathlib
import signal
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'extern' / 'tashrouter'))

from tashrouter.port.localtalk.ltoudp import LtoudpPort
from tashrouter.port.ethertalk.macvtap import MacvtapPort
from tashrouter.router.router import Router

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s: %(message)s')

router = Router('POM68K router', ports=(
    LtoudpPort(seed_network=1, seed_zone_name=b'POM68K'),
    MacvtapPort(macvtap_name='pomtap0', seed_network_min=2,
                seed_network_max=2, seed_zone_names=[b'POM68K']),
))

print('POM68K AppleTalk router: LToUDP <-> pomtap0 (zone "POM68K")')
router.start()
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    pass
finally:
    router.stop()
