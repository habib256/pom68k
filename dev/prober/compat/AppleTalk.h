#pragma once
/*
 * compat/AppleTalk.h — API AppleTalk minimale pour le jeu d'interfaces
 * multiversal de Retro68 (qui ne fournit que 3 globals low-mem AppleTalk).
 *
 * C'est le repli prévu par SPEC.md §9 : les blocs paramètres .MPP/.XPP sont
 * redéclarés au layout MPW ATalk.h (Inside Macintosh: Networking) et la glue
 * (appletalk_glue.c) passe par PBControl. Offsets pinnés : l'en-tête MPP
 * remplace ioNamePtr/ioVRefNum du CntrlParam par userData/reqTID, mais
 * ioRefNum/csCode restent aux offsets Device Manager 24/26.
 */
#include <Multiverse.h>

#pragma pack(push, 2)

typedef struct AddrBlock {
    UInt16 aNet;
    UInt8  aNode;
    UInt8  aSocket;
} AddrBlock;

/* csCodes .MPP/.XPP + refNums bien connus (IM: Networking). */
enum {
    lookupName       = 251,   /* .MPP NBP LookupName                  */
    getAppleTalkInfo = 258,   /* .MPP PGetAppleTalkInfo (drivers >=53) */
    xCall            = 246,   /* .XPP appel ZIP générique              */
    zipGetMyZone     = 7      /* xppSubCode                            */
};
enum {
    mppRefNum = -10,          /* refNum unité .MPP */
    xppRefNum = -41           /* refNum unité .XPP */
};

#define MPP_HEADER              \
    void        *qLink;         \
    short        qType;         \
    short        ioTrap;        \
    Ptr          ioCmdAddr;     \
    ProcPtr      ioCompletion;  \
    OSErr        ioResult;      \
    long         userData;      \
    short        reqTID;        \
    short        ioRefNum;      \
    short        csCode;

typedef struct NBPparms {
    MPP_HEADER
    SInt8 interval;                       /* offset 28 */
    SInt8 count;                          /* offset 29 */
    union {
        Ptr ntQElPtr;
        Ptr entityPtr;                    /* offset 30 */
    } NBPPtrs;
    union {
        struct {
            Ptr   retBuffPtr;             /* offset 34 */
            short retBuffSize;            /* offset 38 */
            short maxToGet;               /* offset 40 */
            short numGotten;              /* offset 42 */
        } Lookup;
        struct {
            Ptr   newSocketPtr;
            SInt8 verifyFlag;
        } Confirm;
    } parm;
} NBPparms;

typedef struct GetAppleTalkInfoParm {
    MPP_HEADER
    short version;                        /* offset 28 : demander 1 */
    Ptr   varsPtr;
    Ptr   DCEPtr;
    short portID;
    long  configuration;
    short selfSend;
    short netLo;
    short netHi;
    long  ourAdd;                         /* AddrBlock: aNet<<16|aNode<<8|aSocket */
    long  routerAddr;
    short numOfPHs;
    short numOfSkts;
    short numNBPEs;
    Ptr   nTQueue;
    short LAlength;
    Ptr   linkAddr;
    Ptr   zoneName;
} GetAppleTalkInfoParm;

typedef union MPPParamBlock {
    NBPparms             NBP;
    GetAppleTalkInfoParm GAIINFO;
} MPPParamBlock;
typedef MPPParamBlock *MPPPBPtr;

/* En-tête .XPP : cmdResult remplace ioNamePtr ; ioVRefNum/ioRefNum/csCode
 * aux offsets Device Manager 22/24/26. */
typedef struct XCallParam {
    void   *qLink;
    short   qType;
    short   ioTrap;
    Ptr     ioCmdAddr;
    ProcPtr ioCompletion;
    OSErr   ioResult;
    long    cmdResult;
    short   ioVRefNum;
    short   ioRefNum;
    short   csCode;
    short   xppSubCode;                   /* offset 28 */
    UInt8   xppTimeOut;                   /* offset 30 */
    UInt8   xppRetry;                     /* offset 31 */
    short   filler1;
    Ptr     zipBuffPtr;                   /* offset 34 : reçoit un Str32 */
    short   zipNumZones;
    UInt8   zipLastFlag;
    UInt8   filler2;
    UInt8   zipInfoField[70];             /* état interne ZIP : zéroer au 1er appel */
} XCallParam;

typedef union XPPParamBlock {
    XCallParam XCALL;
} XPPParamBlock;
typedef XPPParamBlock *XPPParmBlkPtr;

#pragma pack(pop)

/* EntityName (3 × Str32) vient de Multiverse.h (déclaré pour le PPC Toolbox,
 * même forme que celui d'AppleTalk). */

OSErr GetNodeAddress(short *myNode, short *myNet);
OSErr PLookupName(MPPParamBlock *pb, Boolean async);
void  NBPSetEntity(Ptr buffer, const unsigned char *nbpObject,
                   const unsigned char *nbpType, const unsigned char *nbpZone);
OSErr NBPExtract(char *theBuffer, short numInBuf, short whichOne,
                 EntityName *abEntity, AddrBlock *address);
OSErr GetMyZone(XPPParmBlkPtr pb, Boolean async);
