/* SPDX-License-Identifier: GPL-3.0-or-later */
/* CC Remap: the receive tables on the +Drive, one set for the whole unit.
 *
 * The eight tables (ccm_rx, 64 bytes, in ccremap.s) are kept as two files,
 * /cfw/ccremap.a and /cfw/ccremap.b, written in turn: a save cut short by
 * power loss leaves the other one whole, and the loader takes the valid one
 * with the higher sequence number. With neither file valid the tables keep
 * their stock 70-77.
 *
 * ccm_tick runs from the 30 Hz compose check (ev_tick, the UI task). It loads
 * the files once the drive is mounted and free, and saves the tables about a
 * second after the last edit on CC Receive (ccremap.s sets ccm_dirty), only
 * when they changed. The firmware's ekFS calls take the filesystem's mutex
 * themselves, except lookup; they are called only while that mutex is free
 * (a sample load holds it for its whole read) and the drive is mounted. */

typedef unsigned long u32;
typedef long s32;
typedef unsigned char u8;

#define OS_EK_OPEN    0x400cf3a0   /* ekFS open(path, mode, file) */
#define OS_EK_READ    0x400cf0e6   /* ekFS read(buf, len, file) */
#define OS_EK_WRITE   0x400cf146   /* ekFS write(buf, len, file) */
#define OS_EK_CLOSE   0x400cf35e   /* ekFS close(file) */
#define OS_EK_MKDIR   0x400ce034   /* ekFS mkdir(path) */
#define OS_EK_LOOKUP  0x400d0ccc   /* ekFS lookup(path, ...) */
#define OS_EK_LOCK    0x40001884   /* mutex lock(m) */
#define OS_EK_UNLOCK  0x400019b6   /* mutex unlock(m) */
#define OS_EK_MUTEX   0x42686690   /* the filesystem's mutex (its owner first) */
#define OS_EK_MOUNTED 0x420eec50   /* the drive is mounted */

typedef struct { u32 inode, pos, writable, open; } ekfile;
#define EK_OPEN    ((s32 (*)(const char *, const char *, ekfile *))OS_EK_OPEN)
#define EK_READ    ((s32 (*)(void *, s32, ekfile *))OS_EK_READ)
#define EK_WRITE   ((s32 (*)(const void *, s32, ekfile *))OS_EK_WRITE)
#define EK_CLOSE   ((s32 (*)(ekfile *))OS_EK_CLOSE)
#define EK_MKDIR   ((s32 (*)(const char *))OS_EK_MKDIR)
#define EK_LOOKUP  ((s32 (*)(const char *, u32 *, s32, s32, s32))OS_EK_LOOKUP)
#define EK_LOCK    ((void (*)(void *))OS_EK_LOCK)
#define EK_UNLOCK  ((void (*)(void *))OS_EK_UNLOCK)
#define EK_MUTEX   ((void *)OS_EK_MUTEX)
#define EK_OWNER   (*(volatile u32 *)OS_EK_MUTEX)
#define EK_MOUNTED (*(volatile u32 *)OS_EK_MOUNTED)
#define EK_NOENT   0x19            /* lookup: no such file or directory */

#define TABLES     64
#define MAGIC      0x43435231u     /* "CCR1" */
#define RETRY      300             /* ticks after a failed save (10 s) */

/* The 16 bytes of padding keep the whole file in a USB listing, which
 * reports size - 16. */
struct ccm_file {
    u32 magic, size, seq, sum;
    u8 rx[TABLES];
    u8 pad[16];
};

extern u8 ccm_rx[TABLES];
extern volatile u32 ccm_dirty;
extern u8 ccm_edited[TABLES];      /* slots set by hand since power-on */

static const char *const ccm_path[2] = { "/cfw/ccremap.b", "/cfw/ccremap.a" };
static struct ccm_file ccm_img;
static u32 ccm_seq;
static s32 ccm_loaded;             /* 1: loaded, or found none */
u32 ccm_saves, ccm_save_fail;      /* counts, for the USB PEEK */

static u32 sum(const u8 *rx)
{
    u32 s = 0x5eed1234u, i;
    for (i = 0; i < TABLES; i++)
        s = ((s << 5) | (s >> 27)) ^ rx[i];
    return s;
}

static s32 ek_ready(void)
{
    return EK_MOUNTED != 0 && EK_OWNER == 0;
}

/* Read one file into ccm_img; 1 if it is whole and valid. */
static s32 img_read(const char *path)
{
    ekfile f;
    s32 got, i;
    if (EK_OPEN(path, "r", &f) != 0)
        return 0;
    got = EK_READ(&ccm_img, sizeof ccm_img, &f);
    EK_CLOSE(&f);
    if (got < (s32)(sizeof ccm_img - sizeof ccm_img.pad) || ccm_img.magic != MAGIC
        || ccm_img.size != TABLES || ccm_img.sum != sum(ccm_img.rx))
        return 0;
    for (i = 0; i < TABLES; i++)
        if (ccm_img.rx[i] > 127)
            return 0;
    return 1;
}

/* Load the tables: the valid file with the higher seq; with none, keep 70-77.
 * Returns 0 if the drive is not ready yet (try again next tick). */
static s32 store_load(void)
{
    s32 ok0, ok1, pick, i;
    u32 seq0;
    if (!ek_ready())
        return 0;
    ok0 = img_read(ccm_path[0]);
    seq0 = ccm_img.seq;
    ok1 = img_read(ccm_path[1]);             /* ccm_img now holds file 1 */
    if (ok0 && ok1)
        pick = ccm_img.seq > seq0 ? 1 : 0;
    else
        pick = ok1 ? 1 : ok0 ? 0 : -1;
    if (pick == 0 && !img_read(ccm_path[0]))
        pick = -1;
    if (pick >= 0) {
        ccm_seq = ccm_img.seq;
        for (i = 0; i < TABLES; i++)          /* a slot set before the drive was ready keeps its value */
            if (!ccm_edited[i])
                ccm_rx[i] = ccm_img.rx[i];
    }
    ccm_loaded = 1;
    return 1;
}

/* Save the tables over the older file (seq + 1 decides which). */
static s32 store_save(void)
{
    ekfile f;
    u32 ino, seq = ccm_seq + 1;
    s32 r, w, i;
    if (!ek_ready())
        return 0;
    EK_LOCK(EK_MUTEX);
    r = EK_LOOKUP("/cfw", &ino, 0, 0, 0);
    EK_UNLOCK(EK_MUTEX);
    if (r == EK_NOENT && EK_MKDIR("/cfw") != 0)
        return -1;
    for (i = 0; i < TABLES; i++)
        ccm_img.rx[i] = ccm_rx[i];
    for (i = 0; i < (s32)sizeof ccm_img.pad; i++)
        ccm_img.pad[i] = 0;
    ccm_img.magic = MAGIC;
    ccm_img.size = TABLES;
    ccm_img.seq = seq;
    ccm_img.sum = sum(ccm_img.rx);
    if (EK_OPEN(ccm_path[seq & 1], "w", &f) != 0)
        return -1;
    w = EK_WRITE(&ccm_img, sizeof ccm_img, &f);
    EK_CLOSE(&f);
    if (w != (s32)sizeof ccm_img)
        return -1;
    ccm_seq = seq;
    return 1;
}

void ccm_tick(u8 *ctrl)
{
    (void)ctrl;
    if (!ccm_loaded) {
        store_load();
        return;
    }
    if (ccm_dirty == 0)
        return;
    if (--ccm_dirty != 0)
        return;
    switch (store_save()) {
    case 1:
        ccm_saves++;
        break;
    case -1:
        ccm_save_fail++;
        ccm_dirty = RETRY;
        break;
    default:                                 /* the drive is busy: next tick */
        ccm_dirty = 1;
        break;
    }
}
