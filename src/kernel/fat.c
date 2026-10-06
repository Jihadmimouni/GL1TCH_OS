#include "fat.h"
#include "kstring.h"
#include "x86.h"

#define MAX_DEPTH      8
#define SECTOR_SIZE    512
#define FAT_BUF_SECTORS 9   /* this project's build always makes a FAT12
                              * 1.44MB floppy with 9 sectors/FAT */
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_LFN       0x0F
#define FAT_EOC            0x0FF8  /* cluster values >= this mean end of chain */

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
static uint8_t g_fat_buf[FAT_BUF_SECTORS * SECTOR_SIZE];

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

static int _cdecl scan_sector(const uint8_t *buf, fat_visitor_t visitor, void *ctx, int *end_of_dir) {
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
            continue;                       /* deleted entry */
        }
        if (e[11] == FAT_ATTR_LFN) {
            continue;                        /* long filename fragment */
        }
        if (e[11] & FAT_ATTR_VOLUME_ID) {
            continue;                        /* volume label */
        }
        if (e[0] == '.') {
            continue;                        /* "." / ".." */
        }

        fat_format_name(e, de.name);
        de.attr = e[11];
        de.cluster = rd16(e + 26);
        de.size = rd32(e + 28);

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

    if (start_cluster == 0) {
        uint16_t s;
        for (s = 0; s < g_root_dir_sectors; s++) {
            int r;
            if (!read_sector(g_root_dir_lba + s, buf)) {
                return 0;
            }
            r = scan_sector(buf, visitor, ctx, &end_of_dir);
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
                r = scan_sector(buf, visitor, ctx, &end_of_dir);
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
