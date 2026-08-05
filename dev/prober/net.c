/*
 * net.c — état AppleTalk. Voir SPEC.md §4.
 *
 * NOTE DE PORTAGE : les noms de glue/champs AppleTalk varient selon les jeux
 * d'en-têtes. Ce fichier suit l'idiome classique d'Inside Macintosh: Networking
 * (macros NBPinterval/NBPcount/... du MPPParamBlock). À réconcilier avec
 * AppleTalk.h du multiversal au 1er build. Les blocs incertains sont derrière
 * PROBER_ZONES pour pouvoir les désactiver sans casser le reste.
 */
#include "net.h"

#include <AppleTalk.h>
#include <Devices.h>
#include <string.h>
#include <stdio.h>

#define PROBER_ZONES 1     /* passer à 0 si GetMyZone ne compile pas */

/* Str32 (pascal) -> C-string bornée. */
static void p2c(char *dst, const unsigned char *pstr, size_t cap)
{
    size_t n = pstr[0];
    if (n >= cap) n = cap - 1;
    memcpy(dst, pstr + 1, n);
    dst[n] = '\0';
}

/* Un lookup NBP "=:<type>@*" ; rapporte le nombre et chaque tuple trouvé. */
static void lookup_type(Report *r, const char *typeC)
{
    MPPParamBlock pb;
    char          entityBuf[104];          /* 3 x Str32 + marge            */
    static char   retBuf[2048];            /* tampon de réponses NBP       */
    Str32         nbpObj, nbpType, nbpZone;
    OSErr         err;
    short         i, num;
    char          key[40];

    /* Construit les Str32 "=", "<type>", "*". */
    nbpObj[0] = 1;  nbpObj[1] = '=';
    nbpZone[0] = 1; nbpZone[1] = '*';
    { size_t n = strlen(typeC); if (n > 31) n = 31;
      nbpType[0] = (unsigned char)n; memcpy(nbpType + 1, typeC, n); }

    NBPSetEntity((Ptr)entityBuf, nbpObj, nbpType, nbpZone);

    memset(&pb, 0, sizeof pb);
    pb.NBP.interval     = 8;               /* ~ retransmit                 */
    pb.NBP.count        = 3;               /* essais                       */
    pb.NBP.NBPPtrs.entityPtr = (Ptr)entityBuf;
    pb.NBP.parm.Lookup.retBuffPtr  = retBuf;
    pb.NBP.parm.Lookup.retBuffSize = sizeof retBuf;
    pb.NBP.parm.Lookup.maxToGet    = 32;

    err = PLookupName(&pb, false);
    num = (err == noErr) ? pb.NBP.parm.Lookup.numGotten : 0;

    sprintf(key, "%s.count", typeC);
    Report_AddDec(r, "net", key, num, R_INFO);

    for (i = 1; i <= num; i++) {
        EntityName ent;
        AddrBlock  addr;
        char       o[34], t[34], z[34], val[80];
        if (NBPExtract(retBuf, num, i, &ent, &addr) != noErr) break;
        p2c(o, (unsigned char *)&ent.objStr,  sizeof o);
        p2c(t, (unsigned char *)&ent.typeStr, sizeof t);
        p2c(z, (unsigned char *)&ent.zoneStr, sizeof z);
        sprintf(val, "%s:%s@%s %u.%u.%u", o, t, z,
                (unsigned)addr.aNet, (unsigned)addr.aNode,
                (unsigned)addr.aSocket);
        sprintf(key, "%s[%d]", typeC, i - 1);
        Report_Add(r, "net", key, val, R_INFO);
    }
}

void Net_Collect(Report *r)
{
    short mppRef;
    short node = 0, net = 0;
    OSErr e;

    /* Pile AppleTalk montée ? */
    e = OpenDriver("\p.MPP", &mppRef);
    Report_Add(r, "net", "mppOpen", (e == noErr) ? "up" : "down",
               (e == noErr) ? R_INFO : R_WARN);
    if (e != noErr) return;                 /* pas d'AppleTalk : on s'arrête */

    /* Adresse de nœud / réseau. */
    if (GetNodeAddress(&node, &net) == noErr) {
        Report_AddDec(r, "net", "node", node, R_INFO);
        Report_AddDec(r, "net", "net",  net,  R_INFO);
    }

#if PROBER_ZONES
    /* Zone locale (via .XPP). Structure fiddly — vérifier au 1er build. */
    {
        short xppRef;
        if (OpenDriver("\p.XPP", &xppRef) == noErr) {
            XPPParamBlock xpb;
            char zoneBuf[34];
            memset(&xpb, 0, sizeof xpb);
            xpb.XCALL.xppSubCode  = zipGetMyZone;
            xpb.XCALL.xppTimeOut  = 3;
            xpb.XCALL.xppRetry    = 4;
            xpb.XCALL.zipBuffPtr  = zoneBuf;   /* reçoit un Str32          */
            /* zipInfoField déjà zéroé par le memset (exigé au 1er appel) */
            if (GetMyZone((XPPParmBlkPtr)&xpb, false) == noErr) {
                char z[34];
                p2c(z, (unsigned char *)zoneBuf, sizeof z);
                Report_Add(r, "net", "myZone", z, R_INFO);
            }
        }
    }
#endif

    /* Services visibles sur le réseau. */
    lookup_type(r, "AFPServer");
    lookup_type(r, "LaserWriter");
    lookup_type(r, "Workstation");

    /* MacIP / MacTCP présent ? */
    {
        short ippRef;
        OSErr ie = OpenDriver("\p.IPP", &ippRef);
        Report_Add(r, "net", "macIP", (ie == noErr) ? "present" : "absent",
                   R_INFO);
    }
}
