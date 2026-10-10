#include "fat.h"
#include "kstring.h"
#include "x86.h"

#define MAX_DEPTH      8
#define SECTOR_SIZE    512
#define FAT_BUF_SECTORS 9   /* this project's build always makes a FAT12
                              * 1.44MB floppy with 9 sectors/FAT */
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_LFN       0x0F
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_EOC            0x0FF8  /* cluster values >= this mean end of chain */
#define FAT_EOC_MARK       0x0FFF  /* value written to mark a new end of chain */
#define FAT_DELETED        0xE5
#define LFN_CHARS_PER_ENTRY 13
#define LFN_MAX_CHARS       FAT_MAX_LFN  /* from fat.h: fat_dirent_t.lfn's size */

static uint8_t g_drive;
static uint16_t g_sectors_per_track;
static uint16_t g_heads;
static uint16_t g_bytes_per_sector;
static uint8_t g_sectors_per_cluster;
static uint16_t g_reserved_sectors;
static uint8_t g_fat_count;
static uint16_t g_root_entries;
static uint16_t g_sectors_per_fat;
static uint32_t g_root_dir_lba;
static uint16_t g_root_dir_sectors;
static uint32_t g_data_lba;
static uint16_t g_total_clusters;
static uint8_t g_fat_buf[FAT_BUF_SECTORS * SECTOR_SIZE];

/* Location of a 32-byte directory entry slot on disk. */
typedef struct {
    uint32_t lba;
    uint16_t offset;
} dirent_loc_t;

static uint16_t g_cwd_cluster[MAX_DEPTH];
static char g_cwd_name[MAX_DEPTH][FAT_MAX_NAME];
static int g_cwd_depth;

static uint16_t _cdecl rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t _cdecl rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void _cdecl lba_to_chs(uint32_t lba, uint16_t *cyl, uint8_t *head, uint8_t *sector) {
    uint32_t temp;
    *sector = (uint8_t)((lba % g_sectors_per_track) + 1);
    temp = lba / g_sectors_per_track;
    *head = (uint8_t)(temp % g_heads);
    *cyl = (uint16_t)(temp / g_heads);
}

static int _cdecl read_sector(uint32_t lba, void *buf) {
    uint16_t cyl;
    uint8_t head, sector;
    uint8_t retry;

    lba_to_chs(lba, &cyl, &head, &sector);

    for (retry = 0; retry < 3; retry++) {
        if (x86_disk_io(0x02, g_drive, cyl, head, sector, buf)) {
            return 1;
        }
        x86_disk_reset(g_drive);
    }
    return 0;
}

static int _cdecl write_sector(uint32_t lba, void *buf) {
    uint16_t cyl;
    uint8_t head, sector;
    uint8_t retry;

    lba_to_chs(lba, &cyl, &head, &sector);

    for (retry = 0; retry < 3; retry++) {
        if (x86_disk_io(0x03, g_drive, cyl, head, sector, buf)) {
            return 1;
        }
        x86_disk_reset(g_drive);
    }
    return 0;
}

static uint32_t _cdecl cluster_to_lba(uint16_t cluster) {
    return g_data_lba + (uint32_t)(cluster - 2) * g_sectors_per_cluster;
}

static uint16_t _cdecl fat_next_cluster(uint16_t cluster) {
    uint16_t fat_offset = cluster + (cluster / 2);
    uint16_t value = rd16(&g_fat_buf[fat_offset]);

    if (cluster & 1) {
        return (uint16_t)(value >> 4);
    }
    return (uint16_t)(value & 0x0FFF);
}

static void _cdecl fat_format_name(const uint8_t *raw11, char *out) {
    int i;
    int j = 0;

    for (i = 0; i < 8 && raw11[i] != ' '; i++) {
        out[j++] = (char)raw11[i];
    }
    if (raw11[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && raw11[i] != ' '; i++) {
            out[j++] = (char)raw11[i];
        }
    }
    out[j] = '\0';
}

/*
 * Read-only VFAT long-filename support: accumulates the chain of
 * 0x0F-attribute LFN fragment entries that (on a real VFAT directory)
 * immediately precede a short 8.3 entry, and - only if the chain's
 * checksum matches that short entry - hands back the assembled long
 * name for display. This never affects which short name cd/cat/rm/
 * fat_find_file match against; it only changes what `ls` prints.
 */
typedef struct {
    char buf[LFN_MAX_CHARS];
    int collecting;
    int overflow;
    uint8_t checksum;
} lfn_acc_t;

static void _cdecl lfn_reset(lfn_acc_t *lfn) {
    lfn->collecting = 0;
    lfn->overflow = 0;
}

/* Standard VFAT short-name checksum algorithm. */
static uint8_t _cdecl lfn_checksum(const uint8_t *name11) {
    uint8_t sum = 0;
    int i;
    for (i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i]);
    }
    return sum;
}

/* Decodes the UTF-16LE code unit at byte offset off/off+1 in a raw LFN
 * entry. Returns 0 for the 0x0000 terminator; non-ASCII code points
 * degrade to '?' since this console only does plain ASCII text. */
static char _cdecl lfn_char(const uint8_t *raw, int off) {
    uint8_t lo = raw[off];
    uint8_t hi = raw[off + 1];
    if (lo == 0 && hi == 0) {
        return 0;
    }
    if (hi != 0) {
        return '?';
    }
    return (char)lo;
}

static void _cdecl lfn_accumulate(lfn_acc_t *lfn, const uint8_t *raw) {
    int seq = raw[0] & 0x3F;
    uint8_t cksum = raw[13];
    int base;
    int i;

    if (seq == 0 || seq > (LFN_MAX_CHARS / LFN_CHARS_PER_ENTRY)) {
        lfn->collecting = 1;
        lfn->overflow = 1;              /* too long/garbled: fall back to short name */
        return;
    }

    if (!lfn->collecting) {
        lfn->collecting = 1;
        lfn->overflow = 0;
        lfn->checksum = cksum;
        mem_set(lfn->buf, 0, LFN_MAX_CHARS);
    } else if (lfn->checksum != cksum) {
        lfn->overflow = 1;              /* inconsistent chain */
        return;
    }

    base = (seq - 1) * LFN_CHARS_PER_ENTRY;
    for (i = 0; i < LFN_CHARS_PER_ENTRY; i++) {
        int byte_off = (i < 5) ? (1 + i * 2)
                      : (i < 11) ? (14 + (i - 5) * 2)
                      : (28 + (i - 11) * 2);
        char c = lfn_char(raw, byte_off);
        if (c == 0) {
            break;
        }
        if (base + i < LFN_MAX_CHARS - 1) {
            lfn->buf[base + i] = c;
        }
    }
}

/* True if a complete, checksum-valid LFN chain was just collected for
 * the short entry about to be reported (short_name11 = its raw 11-byte
 * on-disk name). */
static int _cdecl lfn_ready(const lfn_acc_t *lfn, const uint8_t *short_name11) {
    if (!lfn->collecting || lfn->overflow) {
        return 0;
    }
    if (lfn_checksum(short_name11) != lfn->checksum) {
        return 0;
    }
    return lfn->buf[0] != '\0';
}

static int _cdecl scan_sector(const uint8_t *buf, fat_visitor_t visitor, void *ctx,
                               int *end_of_dir, lfn_acc_t *lfn) {
    int i;

    for (i = 0; i < SECTOR_SIZE / 32; i++) {
        const uint8_t *e = buf + i * 32;
        fat_dirent_t de;
        int vr;

        if (e[0] == 0x00) {
            *end_of_dir = 1;
            return 0;
        }
        if ((uint8_t)e[0] == 0xE5) {
            lfn_reset(lfn);
            continue;                       /* deleted entry */
        }
        if (e[11] == FAT_ATTR_LFN) {
            lfn_accumulate(lfn, e);
            continue;                        /* long filename fragment */
        }
        if (e[11] & FAT_ATTR_VOLUME_ID) {
            lfn_reset(lfn);
            continue;                        /* volume label */
        }
        if (e[0] == '.') {
            lfn_reset(lfn);
            continue;                        /* "." / ".." */
        }

        fat_format_name(e, de.name);
        de.attr = e[11];
        de.cluster = rd16(e + 26);
        de.size = rd32(e + 28);
        de.wrt_time = rd16(e + 22);
        de.wrt_date = rd16(e + 24);

        if (lfn_ready(lfn, e)) {
            str_copy(de.lfn, lfn->buf);
        } else {
            de.lfn[0] = '\0';
        }
        lfn_reset(lfn);

        vr = visitor(&de, ctx);
        if (vr) {
            return vr;
        }
    }
    return 0;
}

/* start_cluster == 0 means the fixed-location root directory. */
static int _cdecl fat_iterate_dir(uint16_t start_cluster, fat_visitor_t visitor, void *ctx) {
    uint8_t buf[SECTOR_SIZE];
    int end_of_dir = 0;
    lfn_acc_t lfn;

    lfn_reset(&lfn);

    if (start_cluster == 0) {
        uint16_t s;
        for (s = 0; s < g_root_dir_sectors; s++) {
            int r;
            if (!read_sector(g_root_dir_lba + s, buf)) {
                return 0;
            }
            r = scan_sector(buf, visitor, ctx, &end_of_dir, &lfn);
            if (r) {
                return r;
            }
            if (end_of_dir) {
                return 0;
            }
        }
        return 0;
    }

    {
        uint16_t cluster = start_cluster;
        while (cluster >= 2 && cluster < FAT_EOC) {
            uint32_t lba = cluster_to_lba(cluster);
            uint8_t c;
            for (c = 0; c < g_sectors_per_cluster; c++) {
                int r;
                if (!read_sector(lba + c, buf)) {
                    return 0;
                }
                r = scan_sector(buf, visitor, ctx, &end_of_dir, &lfn);
                if (r) {
                    return r;
                }
                if (end_of_dir) {
                    return 0;
                }
            }
            cluster = fat_next_cluster(cluster);
        }
    }
    return 0;
}

typedef struct {
    const char *name;
    fat_dirent_t result;
    int found;
} find_ctx_t;

static int _cdecl find_visitor(const fat_dirent_t *e, void *ctx) {
    find_ctx_t *fc = (find_ctx_t *)ctx;
    if (str_icmp(e->name, fc->name) == 0) {
        fc->result = *e;
        fc->found = 1;
        return 1;
    }
    return 0;
}

static int _cdecl find_child(uint16_t dir_cluster, const char *name, fat_dirent_t *out) {
    find_ctx_t fc;
    fc.name = name;
    fc.found = 0;
    fat_iterate_dir(dir_cluster, find_visitor, &fc);
    if (fc.found) {
        *out = fc.result;
        return 1;
    }
    return 0;
}

int _cdecl fat_init(uint8_t drive) {
    uint8_t boot[SECTOR_SIZE];
    uint16_t spt, heads;
    uint16_t i;
    uint16_t total_sectors16;
    uint32_t total_sectors;

    g_drive = drive;

    if (!x86_disk_reset(drive)) {
        return 0;
    }
    if (!x86_disk_params(drive, &spt, &heads)) {
        return 0;
    }
    if (spt == 0) {
        spt = 18;                            /* standard 1.44MB floppy fallback */
    }
    if (heads == 0) {
        heads = 2;
    }
    g_sectors_per_track = spt;
    g_heads = heads;

    if (!read_sector(0, boot)) {
        return 0;
    }

    g_bytes_per_sector    = rd16(&boot[0x0B]);
    g_sectors_per_cluster = boot[0x0D];
    g_reserved_sectors    = rd16(&boot[0x0E]);
    g_fat_count           = boot[0x10];
    g_root_entries        = rd16(&boot[0x11]);
    g_sectors_per_fat     = rd16(&boot[0x16]);

    if (g_bytes_per_sector == 0) {
        g_bytes_per_sector = SECTOR_SIZE;
    }
    if (g_sectors_per_cluster == 0) {
        g_sectors_per_cluster = 1;
    }

    g_root_dir_lba = (uint32_t)g_reserved_sectors + (uint32_t)g_fat_count * g_sectors_per_fat;
    g_root_dir_sectors = (uint16_t)(((uint32_t)g_root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE);
    g_data_lba = g_root_dir_lba + g_root_dir_sectors;

    total_sectors16 = rd16(&boot[0x13]);
    total_sectors = total_sectors16 ? (uint32_t)total_sectors16 : rd32(&boot[0x20]);
    g_total_clusters = (uint16_t)((total_sectors - g_data_lba) / g_sectors_per_cluster);

    for (i = 0; i < g_sectors_per_fat && i < FAT_BUF_SECTORS; i++) {
        if (!read_sector(g_reserved_sectors + i, g_fat_buf + (uint16_t)i * SECTOR_SIZE)) {
            return 0;
        }
    }

    g_cwd_depth = 0;
    return 1;
}

void _cdecl fat_list_cwd(fat_visitor_t visitor, void *ctx) {
    uint16_t cluster = (g_cwd_depth == 0) ? 0 : g_cwd_cluster[g_cwd_depth - 1];
    fat_iterate_dir(cluster, visitor, ctx);
}

void _cdecl fat_get_cwd(char *out) {
    int i;
    char *p = out;

    *p++ = '/';
    for (i = 0; i < g_cwd_depth; i++) {
        const char *n = g_cwd_name[i];
        while (*n != '\0') {
            *p++ = *n++;
        }
        if (i + 1 < g_cwd_depth) {
            *p++ = '/';
        }
    }
    *p = '\0';
}

int _cdecl fat_change_dir(const char *path, const char **err_msg) {
    uint16_t tmp_cluster[MAX_DEPTH];
    char tmp_name[MAX_DEPTH][FAT_MAX_NAME];
    int tmp_depth;
    const char *p = path;
    char comp[FAT_MAX_NAME];
    int ci;
    int k;

    if (*p == '/') {
        tmp_depth = 0;
        p++;
    } else {
        tmp_depth = g_cwd_depth;
        for (k = 0; k < g_cwd_depth; k++) {
            tmp_cluster[k] = g_cwd_cluster[k];
            str_copy(tmp_name[k], g_cwd_name[k]);
        }
    }

    for (;;) {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        ci = 0;
        while (*p != '\0' && *p != '/' && ci < FAT_MAX_NAME - 1) {
            comp[ci++] = *p++;
        }
        comp[ci] = '\0';

        if (str_cmp(comp, ".") == 0) {
            continue;
        }
        if (str_cmp(comp, "..") == 0) {
            if (tmp_depth > 0) {
                tmp_depth--;
            }
            continue;
        }

        {
            uint16_t parent = (tmp_depth == 0) ? 0 : tmp_cluster[tmp_depth - 1];
            fat_dirent_t de;

            if (!find_child(parent, comp, &de)) {
                *err_msg = "no such directory";
                return 0;
            }
            if (!(de.attr & FAT_ATTR_DIRECTORY)) {
                *err_msg = "not a directory";
                return 0;
            }
            if (tmp_depth >= MAX_DEPTH) {
                *err_msg = "path too deep";
                return 0;
            }
            tmp_cluster[tmp_depth] = de.cluster;
            str_copy(tmp_name[tmp_depth], de.name);
            tmp_depth++;
        }
    }

    g_cwd_depth = tmp_depth;
    for (k = 0; k < tmp_depth; k++) {
        g_cwd_cluster[k] = tmp_cluster[k];
        str_copy(g_cwd_name[k], tmp_name[k]);
    }
    return 1;
}

int _cdecl fat_find_file(const char *path, fat_dirent_t *out, const char **err_msg) {
    uint16_t cur_cluster;
    const char *p = path;
    char comp[FAT_MAX_NAME];
    int ci;
    fat_dirent_t de;
    int have_entry = 0;

    if (*p == '/') {
        cur_cluster = 0;
        p++;
    } else {
        cur_cluster = (g_cwd_depth == 0) ? 0 : g_cwd_cluster[g_cwd_depth - 1];
    }

    for (;;) {
        const char *peek;

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        ci = 0;
        while (*p != '\0' && *p != '/' && ci < FAT_MAX_NAME - 1) {
            comp[ci++] = *p++;
        }
        comp[ci] = '\0';

        if (str_cmp(comp, ".") == 0) {
            continue;
        }
        if (str_cmp(comp, "..") == 0) {
            *err_msg = "'..' is not supported in file paths";
            return 0;
        }

        if (!find_child(cur_cluster, comp, &de)) {
            *err_msg = "no such file or directory";
            return 0;
        }
        have_entry = 1;

        peek = p;
        while (*peek == '/') {
            peek++;
        }
        if (*peek != '\0') {
            if (!(de.attr & FAT_ATTR_DIRECTORY)) {
                *err_msg = "not a directory";
                return 0;
            }
            cur_cluster = de.cluster;
        }
    }

    if (!have_entry) {
        *err_msg = "no file specified";
        return 0;
    }
    if (de.attr & FAT_ATTR_DIRECTORY) {
        *err_msg = "is a directory";
        return 0;
    }

    *out = de;
    return 1;
}

int _cdecl fat_read_file(const fat_dirent_t *entry, fat_sink_t sink, void *ctx) {
    uint16_t cluster = entry->cluster;
    uint32_t remaining = entry->size;
    uint8_t buf[SECTOR_SIZE];

    while (cluster >= 2 && cluster < FAT_EOC && remaining > 0) {
        uint32_t lba = cluster_to_lba(cluster);
        uint8_t c;

        for (c = 0; c < g_sectors_per_cluster && remaining > 0; c++) {
            uint16_t chunk;
            if (!read_sector(lba + c, buf)) {
                return 0;
            }
            chunk = (remaining < SECTOR_SIZE) ? (uint16_t)remaining : SECTOR_SIZE;
            sink(buf, chunk, ctx);
            remaining -= chunk;
        }
        cluster = fat_next_cluster(cluster);
    }
    return 1;
}

/* ---- write support ---------------------------------------------------- */

static void _cdecl fat_set_cluster(uint16_t cluster, uint16_t value) {
    uint16_t fat_offset = cluster + (cluster / 2);
    uint16_t old = rd16(&g_fat_buf[fat_offset]);
    uint16_t merged;

    if (cluster & 1) {
        merged = (uint16_t)((old & 0x000F) | (uint16_t)(value << 4));
    } else {
        merged = (uint16_t)((old & 0xF000) | (value & 0x0FFF));
    }
    g_fat_buf[fat_offset] = (uint8_t)(merged & 0xFF);
    g_fat_buf[fat_offset + 1] = (uint8_t)(merged >> 8);
}

static int _cdecl fat_flush_fat(void) {
    uint8_t copy;
    uint16_t i;

    for (copy = 0; copy < g_fat_count; copy++) {
        uint32_t base = g_reserved_sectors + (uint32_t)copy * g_sectors_per_fat;
        for (i = 0; i < g_sectors_per_fat && i < FAT_BUF_SECTORS; i++) {
            if (!write_sector(base + i, g_fat_buf + (uint16_t)i * SECTOR_SIZE)) {
                return 0;
            }
        }
    }
    return 1;
}

static uint16_t _cdecl fat_alloc_cluster(void) {
    uint16_t c;
    uint16_t limit = (uint16_t)(g_total_clusters + 2);

    for (c = 2; c < limit; c++) {
        if (fat_next_cluster(c) == 0) {
            fat_set_cluster(c, FAT_EOC_MARK);
            return c;
        }
    }
    return 0;
}

static void _cdecl free_chain(uint16_t cluster) {
    while (cluster >= 2 && cluster < FAT_EOC) {
        uint16_t next = fat_next_cluster(cluster);
        fat_set_cluster(cluster, 0);
        cluster = next;
    }
}

static int _cdecl zero_cluster(uint16_t cluster) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t lba = cluster_to_lba(cluster);
    uint8_t c;

    mem_set(buf, 0, SECTOR_SIZE);
    for (c = 0; c < g_sectors_per_cluster; c++) {
        if (!write_sector(lba + c, buf)) {
            return 0;
        }
    }
    return 1;
}

/* Converts a user-typed "NAME" or "NAME.EXT" component into the 11-byte
 * space-padded on-disk form. Returns 0 if the name is empty or doesn't
 * fit the 8.3 scheme. */
static int _cdecl format_name_83(const char *name, uint8_t *raw11) {
    int i = 0;
    int bi = 0;
    int ei = 0;
    char base[8];
    char ext[3];

    if (name[0] == '\0' || name[0] == '.') {
        return 0;
    }

    while (name[i] != '\0' && name[i] != '.') {
        if (bi >= 8) {
            return 0;
        }
        base[bi++] = to_upper(name[i]);
        i++;
    }
    if (bi == 0) {
        return 0;
    }

    if (name[i] == '.') {
        i++;
        while (name[i] != '\0') {
            if (name[i] == '.' || ei >= 3) {
                return 0;
            }
            ext[ei++] = to_upper(name[i]);
            i++;
        }
    }

    mem_set(raw11, ' ', 11);
    for (i = 0; i < bi; i++) {
        raw11[i] = (uint8_t)base[i];
    }
    for (i = 0; i < ei; i++) {
        raw11[8 + i] = (uint8_t)ext[i];
    }
    return 1;
}

/* Builds a brand-new 32-byte directory entry. CrtTime/CrtDate,
 * LastAccessDate and WrtTime/WrtDate are all stamped with the current
 * BIOS RTC time, since creation and last-write are the same event for
 * a freshly made entry. FstClusHI (bytes 20-21) stays 0: this project
 * only ever deals with FAT12, which has no high cluster word. */
static void _cdecl build_dirent_raw(const uint8_t *name11, uint8_t attr,
                                     uint16_t cluster, uint32_t size, uint8_t *raw) {
    uint16_t fat_date, fat_time;

    mem_set(raw, 0, 32);
    mem_copy(raw, name11, 11);
    raw[11] = attr;

    x86_get_datetime(&fat_date, &fat_time);

    raw[14] = (uint8_t)(fat_time & 0xFF);       /* CrtTime */
    raw[15] = (uint8_t)(fat_time >> 8);
    raw[16] = (uint8_t)(fat_date & 0xFF);       /* CrtDate */
    raw[17] = (uint8_t)(fat_date >> 8);
    raw[18] = (uint8_t)(fat_date & 0xFF);       /* LastAccessDate */
    raw[19] = (uint8_t)(fat_date >> 8);
    raw[22] = (uint8_t)(fat_time & 0xFF);       /* WrtTime */
    raw[23] = (uint8_t)(fat_time >> 8);
    raw[24] = (uint8_t)(fat_date & 0xFF);       /* WrtDate */
    raw[25] = (uint8_t)(fat_date >> 8);

    raw[26] = (uint8_t)(cluster & 0xFF);
    raw[27] = (uint8_t)(cluster >> 8);
    raw[28] = (uint8_t)(size & 0xFF);
    raw[29] = (uint8_t)((size >> 8) & 0xFF);
    raw[30] = (uint8_t)((size >> 16) & 0xFF);
    raw[31] = (uint8_t)((size >> 24) & 0xFF);
}

/* Patches an existing 32-byte raw entry (already containing the right
 * name/attr/CrtTime/CrtDate from when it was created) with a new
 * cluster/size and a fresh WrtTime/WrtDate, leaving everything else -
 * including CrtTime/CrtDate - untouched. Used by fat_write_file() when
 * it's updating an entry that already exists, rather than creating one
 * from scratch via build_dirent_raw(). */
static void _cdecl update_dirent_write(uint8_t *raw, uint16_t cluster, uint32_t size) {
    uint16_t fat_date, fat_time;

    x86_get_datetime(&fat_date, &fat_time);

    raw[22] = (uint8_t)(fat_time & 0xFF);       /* WrtTime */
    raw[23] = (uint8_t)(fat_time >> 8);
    raw[24] = (uint8_t)(fat_date & 0xFF);       /* WrtDate */
    raw[25] = (uint8_t)(fat_date >> 8);

    raw[26] = (uint8_t)(cluster & 0xFF);
    raw[27] = (uint8_t)(cluster >> 8);
    raw[28] = (uint8_t)(size & 0xFF);
    raw[29] = (uint8_t)((size >> 8) & 0xFF);
    raw[30] = (uint8_t)((size >> 16) & 0xFF);
    raw[31] = (uint8_t)((size >> 24) & 0xFF);
}

static int _cdecl write_dirent_raw(const dirent_loc_t *loc, const uint8_t *raw32) {
    uint8_t buf[SECTOR_SIZE];
    if (!read_sector(loc->lba, buf)) {
        return 0;
    }
    mem_copy(buf + loc->offset, raw32, 32);
    return write_sector(loc->lba, buf);
}

static int _cdecl mem_cmp11(const uint8_t *a, const uint8_t *b) {
    int i;
    for (i = 0; i < 11; i++) {
        if (a[i] != b[i]) {
            return 1;
        }
    }
    return 0;
}

typedef int (_cdecl *raw_entry_visitor_t)(uint32_t lba, uint16_t off, uint8_t *raw, void *ctx);

/* Calls visitor(lba, offset, raw_entry, ctx) for every 32-byte slot (free
 * or not) in the given directory, root ("cluster 0") or a subdirectory's
 * cluster chain. Stops early if visitor returns nonzero. */
static int _cdecl walk_dir_raw(uint16_t dir_cluster, raw_entry_visitor_t visitor, void *ctx) {
    uint8_t buf[SECTOR_SIZE];
    int i;

    if (dir_cluster == 0) {
        uint16_t s;
        for (s = 0; s < g_root_dir_sectors; s++) {
            uint32_t lba = g_root_dir_lba + s;
            if (!read_sector(lba, buf)) {
                return 0;
            }
            for (i = 0; i < SECTOR_SIZE / 32; i++) {
                int r = visitor(lba, (uint16_t)(i * 32), buf + i * 32, ctx);
                if (r) {
                    return r;
                }
            }
        }
        return 0;
    }

    {
        uint16_t cluster = dir_cluster;
        while (cluster >= 2 && cluster < FAT_EOC) {
            uint32_t lba = cluster_to_lba(cluster);
            uint8_t c;
            for (c = 0; c < g_sectors_per_cluster; c++) {
                uint32_t slba = lba + c;
                if (!read_sector(slba, buf)) {
                    return 0;
                }
                for (i = 0; i < SECTOR_SIZE / 32; i++) {
                    int r = visitor(slba, (uint16_t)(i * 32), buf + i * 32, ctx);
                    if (r) {
                        return r;
                    }
                }
            }
            cluster = fat_next_cluster(cluster);
        }
    }
    return 0;
}

/* Finds, in one pass, both an existing entry named name11 (if any) and
 * the first reusable slot (deleted, or at the unused tail of the
 * directory) to use when creating a new entry. */
typedef struct {
    const uint8_t *name11;
    int found_existing;
    dirent_loc_t existing_loc;
    uint8_t existing_raw[32];
    int found_free;
    dirent_loc_t free_loc;
} dir_search_ctx_t;

static int _cdecl dir_search_visitor(uint32_t lba, uint16_t off, uint8_t *raw, void *vctx) {
    dir_search_ctx_t *ctx = (dir_search_ctx_t *)vctx;

    if (raw[0] == 0x00) {
        if (!ctx->found_free) {
            ctx->found_free = 1;
            ctx->free_loc.lba = lba;
            ctx->free_loc.offset = off;
        }
        return 1;   /* end of directory: nothing valid follows */
    }
    if ((uint8_t)raw[0] == FAT_DELETED) {
        if (!ctx->found_free) {
            ctx->found_free = 1;
            ctx->free_loc.lba = lba;
            ctx->free_loc.offset = off;
        }
        return 0;
    }
    if (raw[11] == FAT_ATTR_LFN || (raw[11] & FAT_ATTR_VOLUME_ID)) {
        return 0;
    }
    if (mem_cmp11(raw, ctx->name11) == 0) {
        ctx->found_existing = 1;
        ctx->existing_loc.lba = lba;
        ctx->existing_loc.offset = off;
        mem_copy(ctx->existing_raw, raw, 32);
        return 1;
    }
    return 0;
}

static int _cdecl any_entry_visitor(uint32_t lba, uint16_t off, uint8_t *raw, void *vctx) {
    int *found = (int *)vctx;
    (void)lba;
    (void)off;

    if (raw[0] == 0x00) {
        return 1;
    }
    if ((uint8_t)raw[0] == FAT_DELETED) {
        return 0;
    }
    if (raw[11] == FAT_ATTR_LFN || (raw[11] & FAT_ATTR_VOLUME_ID)) {
        return 0;
    }
    *found = 1;
    return 1;
}

static int _cdecl dir_is_empty(uint16_t dir_cluster) {
    int found = 0;
    walk_dir_raw(dir_cluster, any_entry_visitor, &found);
    return !found;
}

/* A subdirectory (unlike the fixed-size root) can grow: chain a fresh,
 * zeroed cluster onto its end and hand back the location of its first
 * (now free) slot. The FAT is flushed before returning. */
static int _cdecl grow_dir_and_get_slot(uint16_t dir_cluster, dirent_loc_t *out) {
    uint16_t cluster = dir_cluster;
    uint16_t new_cluster;

    while (fat_next_cluster(cluster) >= 2 && fat_next_cluster(cluster) < FAT_EOC) {
        cluster = fat_next_cluster(cluster);
    }

    new_cluster = fat_alloc_cluster();
    if (new_cluster == 0) {
        return 0;
    }
    if (!zero_cluster(new_cluster)) {
        return 0;
    }
    fat_set_cluster(cluster, new_cluster);
    if (!fat_flush_fat()) {
        return 0;
    }

    out->lba = cluster_to_lba(new_cluster);
    out->offset = 0;
    return 1;
}

/* Writes len bytes to a freshly allocated cluster chain, starting at
 * *first_cluster (set on success; set to 0 if len is 0). */
static int _cdecl write_chain(uint16_t *first_cluster, const uint8_t *data,
                               uint32_t len, const char **err_msg) {
    uint16_t cluster;
    uint32_t remaining = len;
    const uint8_t *src = data;

    if (len == 0) {
        *first_cluster = 0;
        return 1;
    }

    cluster = fat_alloc_cluster();
    if (cluster == 0) {
        *err_msg = "disk full";
        return 0;
    }
    *first_cluster = cluster;

    for (;;) {
        uint32_t lba = cluster_to_lba(cluster);
        uint8_t c;

        for (c = 0; c < g_sectors_per_cluster; c++) {
            uint8_t buf[SECTOR_SIZE];
            uint16_t chunk = (remaining < SECTOR_SIZE) ? (uint16_t)remaining : SECTOR_SIZE;

            if (chunk > 0) {
                mem_copy(buf, src, chunk);
                if (chunk < SECTOR_SIZE) {
                    mem_set(buf + chunk, 0, (uint16_t)(SECTOR_SIZE - chunk));
                }
                if (!write_sector(lba + c, buf)) {
                    *err_msg = "disk error";
                    return 0;
                }
                src += chunk;
                remaining -= chunk;
            }
        }

        if (remaining == 0) {
            return 1;
        }

        {
            uint16_t next = fat_alloc_cluster();
            if (next == 0) {
                *err_msg = "disk full";
                return 0;
            }
            fat_set_cluster(cluster, next);
            cluster = next;
        }
    }
}

/* Writes len bytes at byte offset `offset` within a single cluster,
 * which may span more than one sector if sectors_per_cluster > 1. */
static int _cdecl partial_write_in_cluster(uint16_t cluster, uint16_t offset,
                                            const uint8_t *data, uint16_t len) {
    uint32_t lba = cluster_to_lba(cluster);
    uint16_t sector_idx = (uint16_t)(offset / SECTOR_SIZE);
    uint16_t sector_off = (uint16_t)(offset % SECTOR_SIZE);
    const uint8_t *src = data;
    uint16_t remaining = len;

    while (remaining > 0) {
        uint8_t buf[SECTOR_SIZE];
        uint16_t space = (uint16_t)(SECTOR_SIZE - sector_off);
        uint16_t take = (remaining < space) ? remaining : space;

        if (!read_sector(lba + sector_idx, buf)) {
            return 0;
        }
        mem_copy(buf + sector_off, src, take);
        if (!write_sector(lba + sector_idx, buf)) {
            return 0;
        }

        src += take;
        remaining -= take;
        sector_idx++;
        sector_off = 0;
    }
    return 1;
}

/* Appends len bytes of data after a file's existing content (size
 * existing_size, starting at *first_cluster, which is 0 for an empty
 * file). *first_cluster is updated if the file had no clusters yet. */
static int _cdecl append_to_chain(uint16_t *first_cluster, uint32_t existing_size,
                                   const uint8_t *data, uint32_t len, const char **err_msg) {
    uint16_t cluster = *first_cluster;
    uint32_t cluster_bytes = (uint32_t)g_sectors_per_cluster * SECTOR_SIZE;
    uint32_t used_in_last;
    const uint8_t *src = data;
    uint32_t remaining = len;

    if (len == 0) {
        return 1;
    }

    if (cluster == 0) {
        return write_chain(first_cluster, data, len, err_msg);
    }

    while (fat_next_cluster(cluster) >= 2 && fat_next_cluster(cluster) < FAT_EOC) {
        cluster = fat_next_cluster(cluster);
    }

    used_in_last = existing_size % cluster_bytes;
    if (existing_size != 0 && used_in_last == 0) {
        used_in_last = cluster_bytes;
    }

    if (used_in_last < cluster_bytes) {
        uint32_t space = cluster_bytes - used_in_last;
        uint32_t take = (remaining < space) ? remaining : space;

        if (!partial_write_in_cluster(cluster, (uint16_t)used_in_last, src, (uint16_t)take)) {
            *err_msg = "disk error";
            return 0;
        }
        src += take;
        remaining -= take;
    }

    if (remaining > 0) {
        uint16_t new_first;
        if (!write_chain(&new_first, src, remaining, err_msg)) {
            return 0;
        }
        fat_set_cluster(cluster, new_first);
    }

    return 1;
}

/* Walks path up to but not including its last component, which is
 * handed back unresolved in leaf (it need not exist). */
static int _cdecl resolve_parent(const char *path, uint16_t *parent_cluster,
                                  char *leaf, const char **err_msg) {
    uint16_t cur_cluster;
    const char *p = path;
    char comp[FAT_MAX_NAME];
    int ci;

    if (*p == '/') {
        cur_cluster = 0;
        p++;
    } else {
        cur_cluster = (g_cwd_depth == 0) ? 0 : g_cwd_cluster[g_cwd_depth - 1];
    }

    for (;;) {
        const char *peek;

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            *err_msg = "no name specified";
            return 0;
        }

        ci = 0;
        while (*p != '\0' && *p != '/' && ci < FAT_MAX_NAME - 1) {
            comp[ci++] = *p++;
        }
        comp[ci] = '\0';

        if (str_cmp(comp, ".") == 0) {
            continue;
        }
        if (str_cmp(comp, "..") == 0) {
            *err_msg = "'..' is not supported here";
            return 0;
        }

        peek = p;
        while (*peek == '/') {
            peek++;
        }
        if (*peek == '\0') {
            str_copy(leaf, comp);
            *parent_cluster = cur_cluster;
            return 1;
        }

        {
            fat_dirent_t de;
            if (!find_child(cur_cluster, comp, &de)) {
                *err_msg = "no such directory";
                return 0;
            }
            if (!(de.attr & FAT_ATTR_DIRECTORY)) {
                *err_msg = "not a directory";
                return 0;
            }
            cur_cluster = de.cluster;
        }
    }
}

/* Shared by mkdir/touch/write: resolves the parent dir and 8.3 name for
 * path, then searches that directory for both a conflicting existing
 * entry and a free slot. */
static int _cdecl prepare_create(const char *path, uint16_t *parent, uint8_t *name11,
                                  dir_search_ctx_t *ctx, const char **err_msg) {
    char leaf[FAT_MAX_NAME];

    if (!resolve_parent(path, parent, leaf, err_msg)) {
        return 0;
    }
    if (!format_name_83(leaf, name11)) {
        *err_msg = "invalid name";
        return 0;
    }

    mem_set(ctx, 0, sizeof(*ctx));
    ctx->name11 = name11;
    walk_dir_raw(*parent, dir_search_visitor, ctx);
    return 1;
}

int _cdecl fat_mkdir(const char *path, const char **err_msg) {
    uint16_t parent;
    uint8_t name11[11];
    dir_search_ctx_t ctx;
    uint16_t new_cluster;
    uint8_t raw[32];

    if (!prepare_create(path, &parent, name11, &ctx, err_msg)) {
        return 0;
    }

    if (ctx.found_existing) {
        *err_msg = "already exists";
        return 0;
    }
    if (!ctx.found_free) {
        if (parent == 0) {
            *err_msg = "directory full";
            return 0;
        }
        if (!grow_dir_and_get_slot(parent, &ctx.free_loc)) {
            *err_msg = "disk full";
            return 0;
        }
    }

    new_cluster = fat_alloc_cluster();
    if (new_cluster == 0) {
        *err_msg = "disk full";
        return 0;
    }
    if (!zero_cluster(new_cluster)) {
        *err_msg = "disk error";
        return 0;
    }
    if (!fat_flush_fat()) {
        *err_msg = "disk error";
        return 0;
    }

    build_dirent_raw(name11, FAT_ATTR_DIRECTORY, new_cluster, 0, raw);
    if (!write_dirent_raw(&ctx.free_loc, raw)) {
        *err_msg = "disk error";
        return 0;
    }
    return 1;
}

int _cdecl fat_create_file(const char *path, const char **err_msg) {
    uint16_t parent;
    uint8_t name11[11];
    dir_search_ctx_t ctx;
    uint8_t raw[32];

    if (!prepare_create(path, &parent, name11, &ctx, err_msg)) {
        return 0;
    }

    if (ctx.found_existing) {
        if (ctx.existing_raw[11] & FAT_ATTR_DIRECTORY) {
            *err_msg = "is a directory";
            return 0;
        }
        return 1;
    }

    if (!ctx.found_free) {
        if (parent == 0) {
            *err_msg = "directory full";
            return 0;
        }
        if (!grow_dir_and_get_slot(parent, &ctx.free_loc)) {
            *err_msg = "disk full";
            return 0;
        }
    }

    build_dirent_raw(name11, FAT_ATTR_ARCHIVE, 0, 0, raw);
    if (!write_dirent_raw(&ctx.free_loc, raw)) {
        *err_msg = "disk error";
        return 0;
    }
    return 1;
}

int _cdecl fat_write_file(const char *path, const uint8_t *data, uint32_t len,
                          int append, const char **err_msg) {
    uint16_t parent;
    uint8_t name11[11];
    dir_search_ctx_t ctx;
    uint16_t first_cluster;
    uint32_t total_size;
    uint8_t raw[32];
    dirent_loc_t target_loc;

    if (!prepare_create(path, &parent, name11, &ctx, err_msg)) {
        return 0;
    }

    if (ctx.found_existing && (ctx.existing_raw[11] & FAT_ATTR_DIRECTORY)) {
        *err_msg = "is a directory";
        return 0;
    }

    if (ctx.found_existing && append) {
        first_cluster = rd16(&ctx.existing_raw[26]);
        total_size = rd32(&ctx.existing_raw[28]);
        target_loc = ctx.existing_loc;

        if (!append_to_chain(&first_cluster, total_size, data, len, err_msg)) {
            return 0;
        }
        total_size += len;
    } else {
        if (ctx.found_existing) {
            uint16_t old_cluster = rd16(&ctx.existing_raw[26]);
            free_chain(old_cluster);
            target_loc = ctx.existing_loc;
        } else {
            if (!ctx.found_free) {
                if (parent == 0) {
                    *err_msg = "directory full";
                    return 0;
                }
                if (!grow_dir_and_get_slot(parent, &ctx.free_loc)) {
                    *err_msg = "disk full";
                    return 0;
                }
            }
            target_loc = ctx.free_loc;
        }

        if (!write_chain(&first_cluster, data, len, err_msg)) {
            return 0;
        }
        total_size = len;
    }

    if (!fat_flush_fat()) {
        *err_msg = "disk error";
        return 0;
    }

    if (ctx.found_existing) {
        /* Rewriting an entry that already exists (plain write or
         * append): keep its name/attr/CrtTime/CrtDate as they were,
         * only the cluster/size and WrtTime/WrtDate change. */
        mem_copy(raw, ctx.existing_raw, 32);
        update_dirent_write(raw, first_cluster, total_size);
    } else {
        build_dirent_raw(name11, FAT_ATTR_ARCHIVE, first_cluster, total_size, raw);
    }
    if (!write_dirent_raw(&target_loc, raw)) {
        *err_msg = "disk error";
        return 0;
    }
    return 1;
}

int _cdecl fat_remove(const char *path, const char **err_msg) {
    uint16_t parent;
    char leaf[FAT_MAX_NAME];
    uint8_t name11[11];
    dir_search_ctx_t ctx;
    uint16_t cluster;
    uint8_t buf[SECTOR_SIZE];

    if (!resolve_parent(path, &parent, leaf, err_msg)) {
        return 0;
    }
    if (!format_name_83(leaf, name11)) {
        *err_msg = "invalid name";
        return 0;
    }

    mem_set(&ctx, 0, sizeof(ctx));
    ctx.name11 = name11;
    walk_dir_raw(parent, dir_search_visitor, &ctx);

    if (!ctx.found_existing) {
        *err_msg = "no such file or directory";
        return 0;
    }

    cluster = rd16(&ctx.existing_raw[26]);

    if (ctx.existing_raw[11] & FAT_ATTR_DIRECTORY) {
        int k;
        for (k = 0; k < g_cwd_depth; k++) {
            if (g_cwd_cluster[k] == cluster) {
                *err_msg = "directory is in use";
                return 0;
            }
        }
        if (!dir_is_empty(cluster)) {
            *err_msg = "directory not empty";
            return 0;
        }
    }

    free_chain(cluster);
    if (!fat_flush_fat()) {
        *err_msg = "disk error";
        return 0;
    }

    if (!read_sector(ctx.existing_loc.lba, buf)) {
        *err_msg = "disk error";
        return 0;
    }
    buf[ctx.existing_loc.offset] = FAT_DELETED;
    if (!write_sector(ctx.existing_loc.lba, buf)) {
        *err_msg = "disk error";
        return 0;
    }
    return 1;
}
