/* mount.h — mount and unmount a SCSI bay's volume on request (application
 * context; may use globals and the toolbox freely). */
#pragma once
#include <MacTypes.h>
#include "drv.h"

/* Mount the HFS volume of SCSI id. A drive the ROM already knows (a
 * driver at refNum -(33+id)) is simply PBMountVol'd; a bay the System has
 * never seen gets its partition map read, a drive-queue entry served by
 * our driver, then PBMountVol. On success *driveOut and volName (Str27). */
OSErr agentMount(AgentDriverState *st, short id, short *driveOut,
                 unsigned char *volName);
/* Unmount the volume sitting on SCSI id's drive (fBsyErr while files are
 * open). A drive of ours is then removed from the queue. */
OSErr agentUnmount(AgentDriverState *st, short id);
/* On quit: unmount every drive of ours that still holds a volume. */
void  agentUnmountAll(AgentDriverState *st);
