/*
 * compat/appletalk_glue.c — glue AppleTalk par PBControl (repli SPEC.md §9).
 * NBPSetEntity/NBPExtract sont du pur C (format des tuples LkUp :
 * AddrBlock(4) + énumérateur(1) + 3 pascal-strings — Inside AppleTalk).
 */
#include "AppleTalk.h"

#include <Devices.h>
#include <string.h>

static OSErr callControl(void *pb, Boolean async)
{
    if (async) return PBControlAsync((ParmBlkPtr)pb);
    return PBControlSync((ParmBlkPtr)pb);
}

OSErr PLookupName(MPPParamBlock *pb, Boolean async)
{
    pb->NBP.ioRefNum = mppRefNum;
    pb->NBP.csCode   = lookupName;
    return callControl(pb, async);
}

OSErr GetMyZone(XPPParmBlkPtr pb, Boolean async)
{
    pb->XCALL.ioRefNum   = xppRefNum;
    pb->XCALL.csCode     = xCall;
    pb->XCALL.xppSubCode = zipGetMyZone;
    return callControl(pb, async);
}

/* PGetAppleTalkInfo (csCode 258, drivers >= 53 — OK sur les images System 7
 * cibles). ourAdd est un AddrBlock sérialisé : net(16) | node(8) | socket(8). */
OSErr GetNodeAddress(short *myNode, short *myNet)
{
    GetAppleTalkInfoParm pb;
    OSErr e;

    memset(&pb, 0, sizeof pb);
    pb.ioRefNum = mppRefNum;
    pb.csCode   = getAppleTalkInfo;
    pb.version  = 1;
    e = callControl(&pb, false);
    if (e != noErr) return e;
    *myNet  = (short)((pb.ourAdd >> 16) & 0xFFFF);
    *myNode = (short)((pb.ourAdd >> 8) & 0xFF);
    return noErr;
}

/* Trois pascal-strings accolées dans le tampon entité. */
void NBPSetEntity(Ptr buffer, const unsigned char *nbpObject,
                  const unsigned char *nbpType, const unsigned char *nbpZone)
{
    unsigned char *p = (unsigned char *)buffer;

    memcpy(p, nbpObject, (size_t)nbpObject[0] + 1); p += nbpObject[0] + 1;
    memcpy(p, nbpType,   (size_t)nbpType[0] + 1);   p += nbpType[0] + 1;
    memcpy(p, nbpZone,   (size_t)nbpZone[0] + 1);
}

/* Copie une pascal-string bornée à 32 caractères (Str32 = Byte[33]). */
static const unsigned char *extractPStr(const unsigned char *q, Byte *dst)
{
    size_t n = q[0];
    if (n > 32) n = 32;
    dst[0] = (Byte)n;
    memcpy(dst + 1, q + 1, n);
    return q + q[0] + 1;
}

OSErr NBPExtract(char *theBuffer, short numInBuf, short whichOne,
                 EntityName *abEntity, AddrBlock *address)
{
    const unsigned char *p = (const unsigned char *)theBuffer;
    short i;

    if (whichOne < 1 || whichOne > numInBuf) return -3103; /* extractErr */

    for (i = 1; i <= numInBuf; i++) {
        const unsigned char *q = p + 5;    /* saute AddrBlock + énumérateur */
        if (i == whichOne) {
            memcpy(address, p, 4);
            q = extractPStr(q, abEntity->objStr);
            q = extractPStr(q, abEntity->typeStr);
            (void)extractPStr(q, abEntity->zoneStr);
            return noErr;
        }
        q += q[0] + 1;                     /* objet */
        q += q[0] + 1;                     /* type  */
        q += q[0] + 1;                     /* zone  */
        p = q;
    }
    return -3103;
}
