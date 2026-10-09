# NetProbe — a real MacTCP DNS/TCP client

This small 68k application checks the network from inside System 7/MacTCP.
It uses Apple's Device Manager calls to `.IPP`, with the parameter-block ABI
from [Apple Universal Interfaces 3.4.1 MacTCP.h](https://github.com/elliotnunn/UniversalInterfaces/blob/master/3.4.1/Universal/Interfaces/CIncludes/MacTCP.h).
The relevant commands are also documented in the
[MacTCP Programmer's Guide](https://bitsavers.org/pdf/apple/mac/developer/Networking/MacTCP_Programmers_Guide_1989.pdf).
The app contains its own bounded A-record DNS client; it does not call Apple's
DNR library or change the MacTCP control panel's DNS list.

Build using the user-provided Retro68 toolchain:

```sh
cmake -S dev/netprobe -B dev/netprobe/build \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/dev/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
cmake --build dev/netprobe/build --parallel
```

`NetProbe.bin` is a MacBinary with ordinary application resource/data forks.
The host scenario prepares an ordinary private TOOLS volume carrying the
original MacTCP Ping's exact two forks, NetProbe and `NetProbe.cfg`, using
`HfsBlankVolume` and `HfsInject`. The Finder launches it; no emulator-specific command,
trap, memory mailbox or forged TCP state is used. Build products are ignored.

The configuration is one ASCII line: `IPv4 DNS-port TCP-port`. The DNS server
receives an A/IN question for `pom68k.test` on an ephemeral test port. The
application validates the response's transaction ID, question, source and
answer, then connects to the returned address through TCPActiveOpen. TCP data
is compared exactly before TCPClose/TCPRelease. It writes all API results to
`NetProbe.txt` next to the application and stays resident, so a failed pending
asynchronous command cannot retain a pointer to freed application memory.

The host fixture runs its DNS and TCP peers on its own IPv4 interface and
ordinary nonblocking sockets. No public DNS server, privileged port or address
alias is required. A host IPv4 route and the compiled app are prerequisites.
The guest address still comes solely from RARP in MacTCP Server mode; the
scenario separately enters `192.168.151.1` as router before selecting Server.
The configuration file supplies the application's remote service endpoints.

This probe covers the selected A-record and TCP conversation. Apple's DNR,
control-panel DNS configuration, general DNS compression/record types, public
Internet services, vintage web browsers and TLS are separate consumers.
