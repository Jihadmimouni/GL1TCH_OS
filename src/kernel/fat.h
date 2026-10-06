#pragma once
#include "stdint.h"

#define FAT_ATTR_DIRECTORY 0x10
#define FAT_MAX_NAME       13  /* "12345678.123" + NUL */

typedef struct {
    char name[FAT_MAX_NAME];
    uint8_t attr;
    uint16_t cluster;
    uint32_t size;
} fat_dirent_t;

/* Returns 1 on success, 0 if the boot sector or FAT could not be read. */
int _cdecl fat_init(uint8_t drive);

/*
 * Calls visitor(entry, ctx) for each non-hidden-system entry in the
 * current directory ("." and ".." are skipped; the caller tracks its
 * own path). Stops early if visitor returns nonzero.
 */
typedef int (_cdecl *fat_visitor_t)(const fat_dirent_t *entry, void *ctx);
void _cdecl fat_list_cwd(fat_visitor_t visitor, void *ctx);

/* Writes the current working directory path (e.g. "/", "/sub/dir") to out. */
void _cdecl fat_get_cwd(char *out);

/*
 * Changes directory. path may be absolute (leading '/') or relative,
 * with '/'-separated components and ".."/"." supported. Returns 1 on
 * success; on failure the current directory is unchanged and *err_msg
 * is set.
 */
int _cdecl fat_change_dir(const char *path, const char **err_msg);

/*
 * Resolves path to a file (not a directory) relative to the current
 * directory. Returns 1 on success; on failure *err_msg is set.
 */
int _cdecl fat_find_file(const char *path, fat_dirent_t *out, const char **err_msg);

/*
 * Reads the file described by entry, calling sink(data, len, ctx) for
 * each chunk read (data is not NUL-terminated). Returns 1 on success.
 */
typedef void (_cdecl *fat_sink_t)(const uint8_t *data, uint16_t len, void *ctx);
int _cdecl fat_read_file(const fat_dirent_t *entry, fat_sink_t sink, void *ctx);
