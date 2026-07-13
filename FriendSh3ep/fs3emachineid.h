#ifndef FS3EMACHINEID_H
#define FS3EMACHINEID_H

/*
 * fs3emachineid.h - Best-effort, lightweight per-machine XOR key.
 *
 * NOT cryptography. This exists purely so a copied account.dat's saved
 * access tokens don't work verbatim if the file is carried over to a
 * different machine (see FS3EApp_SaveAccount/LoadAccount's token
 * hex+XOR encoding in friendsh3ep.c) -- a deterrent against casual
 * copy-paste, nothing more. Anyone willing to read this source (or run
 * a debugger) can trivially recover the key.
 *
 * The key is folded (repeated XOR, see fs3emachineid.c's FoldBytes) from
 * whatever of the following the boot volume's underlying device driver
 * will hand back -- both are read-only, "generic block device" queries
 * that essentially every scsi.device/ide.device/CF-driver implements
 * (the exact same commands HDToolBox and friends rely on), not anything
 * floppy-specific despite living in devices/trackdisk.h:
 *   - TD_GETGEOMETRY: struct DriveGeometry (sector size, cylinders,
 *     heads, etc.)
 *   - block 0's RigidDiskBlock, if present: rdb_DiskVendor/Product/
 *     Revision (drive model/firmware strings, not a serial number).
 *
 * Always fills *key* with something, even on total failure (an all-zero
 * key, i.e. an effective no-op XOR) -- a transient I/O hiccup or a
 * device driver that doesn't support these commands must never make a
 * machine unable to decode a token it just encoded itself.
 */

#include <exec/types.h>

#define FS3EMACHINEID_KEYLEN 16

void FS3EMachineId_GetKey(UBYTE key[FS3EMACHINEID_KEYLEN]);

#endif /* FS3EMACHINEID_H */
