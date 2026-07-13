/*
 * fs3emachineid.c - Best-effort, lightweight per-machine XOR key.
 * See fs3emachineid.h for what this is (and, importantly, isn't) for.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <exec/io.h>
#include <exec/memory.h>

#include <devices/trackdisk.h>
#include <devices/hardblocks.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include "fs3emachineid.h"

/* A BPTR is a small integer (also satisfies "looks like a valid BPTR")
 * when e.g. newcon-handler's dn_Startup encodes a mode flag rather than
 * a real pointer -- dn_Startup > 1024 rules that case out first, same
 * defensive pattern used by the NDK's own DAControl/trackfile example
 * (goodies/raw_disk.c). */
#define IS_VALID_BPTR_ADDRESS(ptr) (((ULONG)(ptr) & 0xC0000000) == 0)

/* Repeated/folding XOR: every source byte lands on key[i % KEYLEN],
 * i counting across ALL calls for one key (not reset per call) -- so
 * geometry and RDB bytes both contribute to every key byte rather than
 * the second FoldBytes call just overwriting the first's work. */
static void FoldBytes(UBYTE *key, ULONG *pos, const UBYTE *src, ULONG srcLen)
{
    ULONG i;
    for (i = 0; i < srcLen; i++) {
        key[(*pos) % FS3EMACHINEID_KEYLEN] ^= src[i];
        (*pos)++;
    }
}

/* Resolves the device driver name + unit backing the "SYS:" boot volume,
 * e.g. "scsi.device"/0 -- NOT necessarily "trackdisk.device"; that name
 * only appears in the header these command/struct definitions happen to
 * live in (see fs3emachineid.h).
 *
 * "SYS:" is virtually never itself a DLT_DEVICE DosList entry -- it's an
 * assign (possibly chained through more assigns) pointing at whichever
 * volume was booted, so looking it up by name in the LDF_DEVICES list
 * (an earlier version of this function did exactly that, and always came
 * up empty -- see the all-zero key this was chasing) reliably finds
 * nothing. Instead: Lock("SYS:") lets DOS itself resolve every assign/
 * volume indirection for us, handing back a FileLock whose fl_Task is
 * the *handler process* actually serving that volume -- the DLT_DEVICE
 * entry for the same physical partition shares that exact task once the
 * filesystem is mounted (which it must be, since SYS: just resolved), so
 * matching dol_Task across the LDF_DEVICES list finds the right entry
 * without needing to know its name at all.
 *
 * Entirely self-contained: takes/releases the DosList lock itself and
 * copies the device name into nameBuf before returning, so the caller
 * never has to reason about DOS-owned memory lifetimes. Returns FALSE
 * (nameBuf untouched) if anything along the way couldn't be resolved. */
static BOOL FS3E_FindBootDevice(char *nameBuf, ULONG nameBufSize, ULONG *unitOut)
{
    BPTR   sysLock;
    struct MsgPort *handlerTask = NULL;
    struct DosList *dol;
    struct DeviceNode *dn = NULL;
    struct FileSysStartupMsg *fssm = NULL;
    STRPTR deviceName = NULL;
    BOOL   ok = FALSE;

    sysLock = Lock((STRPTR)"SYS:", ACCESS_READ);
    if (!sysLock) {
        printf("FS3EMachineId: Lock(\"SYS:\") failed\n");
        return FALSE;
    }
    handlerTask = ((struct FileLock *)BADDR(sysLock))->fl_Task;
    UnLock(sysLock);
    printf("FS3EMachineId: SYS: handler task=%08lx\n", (unsigned long)handlerTask);

    dol = LockDosList(LDF_DEVICES | LDF_READ);
    while ((dol = NextDosEntry(dol, LDF_DEVICES)) != NULL) {
        if (dol->dol_Task == handlerTask) {
            dn = (struct DeviceNode *)dol;
            break;
        }
    }

    if (!dn) {
        printf("FS3EMachineId: no LDF_DEVICES entry shares SYS:'s handler task\n");
        UnLockDosList(LDF_DEVICES | LDF_READ);
        return FALSE;
    }
    printf("FS3EMachineId: matched device node, dn_Startup=%08lx\n",
           (unsigned long)dn->dn_Startup);

    if ((ULONG)dn->dn_Startup > 1024 && IS_VALID_BPTR_ADDRESS(dn->dn_Startup))
        fssm = (struct FileSysStartupMsg *)BADDR(dn->dn_Startup);

    if (fssm && TypeOfMem((APTR)fssm) != 0 &&
        IS_VALID_BPTR_ADDRESS(fssm->fssm_Device))
    {
        deviceName = ((STRPTR)BADDR(fssm->fssm_Device)) + 1;
        if (TypeOfMem((APTR)deviceName) == 0)
            deviceName = NULL;
    }

    if (deviceName) {
        strncpy(nameBuf, deviceName, nameBufSize - 1);
        nameBuf[nameBufSize - 1] = '\0';
        *unitOut = fssm->fssm_Unit;
        ok = TRUE;
    }

    printf("FS3EMachineId: resolved device=\"%s\" unit=%lu\n",
           ok ? nameBuf : "(none)", ok ? (unsigned long)*unitOut : 0UL);

    UnLockDosList(LDF_DEVICES | LDF_READ);
    return ok;
}

void FS3EMachineId_GetKey(UBYTE key[FS3EMACHINEID_KEYLEN])
{
    char   deviceName[64];
    ULONG  unit = 0;
    ULONG  pos = 0;
    struct MsgPort  *port;
    struct IOExtTD  *io;

    memset(key, 0, FS3EMACHINEID_KEYLEN); /* safe no-op-XOR fallback */

    if (!FS3E_FindBootDevice(deviceName, sizeof(deviceName), &unit))
        return;

    port = CreateMsgPort();
    if (!port) { printf("FS3EMachineId: CreateMsgPort failed\n"); return; }

    io = (struct IOExtTD *)CreateIORequest(port, sizeof(struct IOExtTD));
    if (!io) {
        printf("FS3EMachineId: CreateIORequest failed\n");
        DeleteMsgPort(port);
        return;
    }

    if (OpenDevice((STRPTR)deviceName, unit, (struct IORequest *)io, 0) != 0) {
        printf("FS3EMachineId: OpenDevice(\"%s\", %lu) failed\n",
               deviceName, (unsigned long)unit);
        DeleteIORequest((struct IORequest *)io);
        DeleteMsgPort(port);
        return;
    }

    {
        struct DriveGeometry geom;
        BYTE err;

        memset(&geom, 0, sizeof(geom));
        io->iotd_Req.io_Command = TD_GETGEOMETRY;
        io->iotd_Req.io_Data    = &geom;
        io->iotd_Req.io_Length  = sizeof(geom);
        err = DoIO((struct IORequest *)io);
        if (err == 0) {
            printf("FS3EMachineId: TD_GETGEOMETRY ok: sectorsize=%lu cyl=%lu heads=%lu\n",
                   (unsigned long)geom.dg_SectorSize,
                   (unsigned long)geom.dg_Cylinders,
                   (unsigned long)geom.dg_Heads);
            FoldBytes(key, &pos, (UBYTE *)&geom.dg_SectorSize,   sizeof(ULONG));
            FoldBytes(key, &pos, (UBYTE *)&geom.dg_TotalSectors, sizeof(ULONG));
            FoldBytes(key, &pos, (UBYTE *)&geom.dg_Cylinders,    sizeof(ULONG));
            FoldBytes(key, &pos, (UBYTE *)&geom.dg_CylSectors,   sizeof(ULONG));
            FoldBytes(key, &pos, (UBYTE *)&geom.dg_Heads,        sizeof(ULONG));
            FoldBytes(key, &pos, (UBYTE *)&geom.dg_TrackSectors, sizeof(ULONG));
            FoldBytes(key, &pos, &geom.dg_DeviceType, sizeof(UBYTE));
        } else {
            printf("FS3EMachineId: TD_GETGEOMETRY failed, io_Error=%d\n", (int)err);
        }
    }

    {
        UBYTE block0[512];
        BYTE  err;

        memset(block0, 0, sizeof(block0));
        io->iotd_Req.io_Command = CMD_READ;
        io->iotd_Req.io_Data    = block0;
        io->iotd_Req.io_Length  = sizeof(block0);
        io->iotd_Req.io_Offset  = 0;
        io->iotd_Req.io_Actual  = 0;
        err = DoIO((struct IORequest *)io);
        if (err == 0) {
            struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)block0;
            printf("FS3EMachineId: CMD_READ block0 ok, rdb_ID=%08lx (want %08lx)\n",
                   (unsigned long)rdb->rdb_ID, (unsigned long)IDNAME_RIGIDDISK);
            if (rdb->rdb_ID == IDNAME_RIGIDDISK) {
                FoldBytes(key, &pos, (UBYTE *)rdb->rdb_DiskVendor,
                          sizeof(rdb->rdb_DiskVendor));
                FoldBytes(key, &pos, (UBYTE *)rdb->rdb_DiskProduct,
                          sizeof(rdb->rdb_DiskProduct));
                FoldBytes(key, &pos, (UBYTE *)rdb->rdb_DiskRevision,
                          sizeof(rdb->rdb_DiskRevision));
            }
        } else {
            printf("FS3EMachineId: CMD_READ block0 failed, io_Error=%d\n", (int)err);
        }
    }

    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
}
