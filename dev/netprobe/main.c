/* Real MacTCP PBControl client; Apple Universal Interfaces 3.4.1 MacTCP.h
 * defines the wire ABI. This subset avoids requiring the optional Apple SDK.
 * DNS goes through UDPCreate/Write/Read, then its A record selects the TCP
 * destination. The host fixture supplies the endpoint ports in NetProbe.cfg.
 * No emulator-specific trap, device command or low-memory mailbox is used. */
#include <Devices.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <OSUtils.h>
#include <Files.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* MacTCP's common parameter-block layout; command data starts at byte 32. */
typedef struct __attribute__((packed, aligned(2))) {
    uint8_t reserved[12]; void *completion; volatile int16_t result;
    void *name; int16_t vref, ref, command; uint32_t stream;
    uint8_t args[128];
} TcpPb;
_Static_assert(offsetof(TcpPb, ref)==24 && offsetof(TcpPb, args)==32,
               "MacTCP Device Manager ABI");
static TcpPb pb;
static int16_t driver;
static uint8_t udpBuffer[8192], tcpBuffer[8192];
static char report[1024], line[90];
static WindowPtr window;
static size_t used;
static uint32_t stream;

static void put16(size_t n, uint16_t v) { pb.args[n]=v>>8; pb.args[n+1]=v; }
static void put32(size_t n, uint32_t v) {
    pb.args[n]=v>>24; pb.args[n+1]=v>>16; pb.args[n+2]=v>>8; pb.args[n+3]=v;
}
static uint16_t get16(const uint8_t *p) { return ((uint16_t)p[0]<<8)|p[1]; }
static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void begin(int16_t command) {
    memset(&pb,0,sizeof pb); pb.ref=driver; pb.command=command; pb.stream=stream;
}
static void show(const char *stage, long value) {
    int n=snprintf(line,sizeof line,"%s: %ld",stage,value);
    if (used+strlen(line)+2<sizeof report) used+=sprintf(report+used,"%s\r",line);
    SetPort(window); EraseRect(&window->portRect); MoveTo(10,25);
    DrawText(line,0,(short)n);
}
static int16_t call(void) {
    uint32_t until=TickCount()+1800; EventRecord event;
    int16_t e=PBControlAsync((ParmBlkPtr)&pb);
    if (e) return e;
    while (pb.result>0 && (int32_t)(TickCount()-until)<0)
        WaitNextEvent(0,&event,1,NULL);
    /* Command-level timeouts below normally finish first. A guard timeout
     * is a failure, and the report is written before the app remains alive. */
    return pb.result>0 ? -23016 : pb.result;
}
static int readConfig(uint32_t *ip, uint16_t *dns, uint16_t *tcp) {
    char data[160]={0}; long n=159; short ref;
    unsigned a,b,c,d,dp,tp;
    if (FSOpen("\pNetProbe.cfg",0,&ref)) return 0;
    FSRead(ref,&n,data); FSClose(ref);
    if (sscanf(data,"%u.%u.%u.%u %u %u",&a,&b,&c,&d,&dp,&tp)!=6 ||
        a>255 || b>255 || c>255 || d>255 || !dp || dp>65535 || !tp || tp>65535)
        return 0;
    *ip=((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)c<<8)|d;
    *dns=dp; *tcp=tp; return 1;
}
static int dnsQuery(uint32_t server, uint16_t port, uint32_t *resolved) {
    /* RFC 1035 A/IN question for pom68k.test, a reserved local test name. */
    uint8_t query[]={0x68,0x4b,1,0,0,1,0,0,0,0,0,0,6,'p','o','m','6','8','k',4,'t','e','s','t',0,0,1,0,1};
    struct __attribute__((packed,aligned(2))) { uint16_t n; const void *p; } wds[2];
    int16_t e; uint8_t *answer; uint16_t size; uint32_t ip=0;
    begin(20); put32(0,(uint32_t)udpBuffer); put32(4,sizeof udpBuffer);
    e=call(); show("UDPCreate",e); if(e) return 0; stream=pb.stream;
    wds[0].n=sizeof query; wds[0].p=query; wds[1].n=0; wds[1].p=NULL;
    begin(23); put32(2,server); put16(6,port); put32(8,(uint32_t)wds); pb.args[12]=1;
    e=call(); show("DNS send",e); if(e) return 0;
    begin(21); put16(0,15);
    e=call(); show("DNS receive",e); if(e) return 0;
    answer=(uint8_t *)get32(pb.args+8); size=get16(pb.args+12);
    /* The controlled server echoes the exact question and one compressed A
     * answer. Bound every read and require transaction, flags and source. */
    if(size==sizeof query+16 && get32(pb.args+2)==server && get16(pb.args+6)==port &&
       !memcmp(answer,query,2) && get16(answer+2)==0x8180 && get16(answer+4)==1 &&
       get16(answer+6)==1 && !get16(answer+8) && !get16(answer+10) &&
       !memcmp(answer+12,query+12,sizeof query-12) &&
       get16(answer+sizeof query)==0xc00c && get16(answer+sizeof query+2)==1 &&
       get16(answer+sizeof query+4)==1 && get16(answer+sizeof query+10)==4)
        ip=get32(answer+sizeof query+12);
    /* UDPRead lends its buffer to the client until UDPBfrReturn. */
    begin(22); put32(8,(uint32_t)answer); e=call();
    if(e) return 0; /* Do not reuse a potentially pending parameter block. */
    begin(24); e=call(); if(e) return 0; stream=0;
    show("DNS resolved",(long)ip); *resolved=ip; return !e && ip==server;
}
static int tcpExchange(uint32_t ip,uint16_t port) {
    static const char request[]="POM68K MacTCP DNS-to-TCP request\r\n";
    static const char expected[]="POM68K host TCP reply after DNS\r\n";
    struct __attribute__((packed,aligned(2))) { uint16_t n; const void *p; } wds[2];
    char received[sizeof expected]={0}; size_t total=0; int16_t e;
    begin(30); put32(0,(uint32_t)tcpBuffer); put32(4,sizeof tcpBuffer);
    e=call(); show("TCPCreate",e); if(e) return 0; stream=pb.stream;
    begin(32); pb.args[0]=15; pb.args[1]=1; pb.args[2]=0xc0; pb.args[3]=15;
    put32(4,ip); put16(8,port); pb.args[19]=64;
    e=call(); show("TCP connect",e); if(e) return 0;
    wds[0].n=sizeof request-1; wds[0].p=request; wds[1].n=0; wds[1].p=NULL;
    begin(34); pb.args[0]=15; pb.args[1]=1; pb.args[2]=0xc0; pb.args[3]=1;
    put32(6,(uint32_t)wds); e=call(); show("TCP send",e); if(e) return 0;
    while(total<sizeof expected-1) {
        uint16_t n;
        begin(37); pb.args[0]=15; put32(4,(uint32_t)(received+total));
        put16(8,(uint16_t)(sizeof expected-1-total));
        e=call(); n=get16(pb.args+8); if(e || !n || n>sizeof expected-1-total) break;
        total+=n;
    }
    show("TCP received bytes",(long)total);
    if(e || total!=sizeof expected-1 || memcmp(received,expected,total)) return 0;
    begin(38); pb.args[0]=15; pb.args[1]=1; pb.args[2]=0xc0;
    e=call(); show("TCP close",e); if(e) return 0;
    begin(42); e=call(); show("TCP release",e); stream=0; return !e;
}
static void writeReport(void) {
    short ref; long n=(long)used;
    Create("\pNetProbe.txt",0,'ttxt','TEXT');
    if(!FSOpen("\pNetProbe.txt",0,&ref)) {
        SetEOF(ref,0); FSWrite(ref,&n,report); FSClose(ref); FlushVol(NULL,0);
    }
}
int main(void) {
    uint32_t server=0,resolved=0; uint16_t dns=0,tcp=0; int ok=0; EventRecord event;
    Rect bounds={60,40,140,580};
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); TEInit();
    InitDialogs(NULL); InitCursor();
    window=NewWindow(NULL,&bounds,"\pNetProbe",true,noGrowDocProc,(WindowPtr)-1,true,0);
    show("Config",readConfig(&server,&dns,&tcp));
    if(server && dns && tcp) {
        int16_t e=OpenDriver("\p.IPP",&driver); show("OpenDriver",e);
        if(!e && dnsQuery(server,dns,&resolved)) ok=tcpExchange(resolved,tcp);
    }
    show(ok ? "PASS" : "FAIL",ok); writeReport();
    /* Stay resident: an outstanding failed command must not retain a pointer
     * to memory returned to the Finder. The test tears down its whole machine. */
    for(;;) WaitNextEvent(0,&event,60,NULL);
}
