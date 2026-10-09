# Production link sources, independent of POM68K_TESTS.
set(POM68K_ETHERNET_SOURCES
    src/DaynaPort.cpp       # SCSI/Link Ethernet target
    src/EtherLink.cpp       # IP/ARP/RARP gateway framing
    src/EtherTalkLink.cpp   # AppleTalk framing
    src/EthernetCapture.cpp # bounded passive host observer
)
