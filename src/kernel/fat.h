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

/*
 * Creates an empty directory at path (relative or absolute). The parent
 * directory must already exist. Returns 1 on success; on failure
 * *err_msg is set.
 */
int _cdecl fat_mkdir(const char *path, const char **err_msg);

/*
 * Creates an empty file at path if it doesn't already exist. Succeeds
 * as a no-op if a file of that name is already there. Returns 1 on
 * success; on failure (e.g. a directory of that name exists) *err_msg
 * is set.
 */
int _cdecl fat_create_file(const char *path, const char **err_msg);

/*
 * Writes len bytes from data to the file at path, creating it if
 * necessary. If append is 0 the file's previous contents (if any) are
 * discarded; if append is 1 the data is added after them. Returns 1 on
 * success; on failure *err_msg is set.
 */
int _cdecl fat_write_file(const char *path, const uint8_t *data, uint32_t len,
                          int append, const char **err_msg);

/*
 * Removes the file or empty directory at path. Returns 1 on success;
 * on failure (no such entry, directory not empty, or directory is the
 * current/an ancestor directory) *err_msg is set.
 */
int _cdecl fat_remove(const char *path, const char **err_msg);

/*
 * Reports total and free space on the filesystem, in bytes, via
 * *total_bytes/*free_bytes. Free space is computed by walking the FAT
 * and counting unallocated clusters. Always succeeds (returns 1).
 */
int _cdecl fat_get_space(uint32_t *total_bytes, uint32_t *free_bytes);
