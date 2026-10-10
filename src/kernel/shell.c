#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "kstring.h"
#include "calc.h"
#include "fat.h"
#include "x86.h"

#define LINE_MAX 128
#define MAX_ARGS 8

/* Prints v zero-padded to two digits (0-99). u32_to_str() doesn't
 * zero-pad, so this is a tiny local helper rather than a change to
 * shared string code. */
static void _cdecl print_2d(uint16_t v) {
    if (v < 10) {
        con_putc('0');
    }
    con_print_u32(v, 10);
}

/* Prints an on-disk FAT date/time pair (see x86_get_datetime's comment
 * in x86.h for the bit layout) as "YYYY-MM-DD HH:MM". */
static void _cdecl print_fat_datetime(uint16_t fdate, uint16_t ftime) {
    uint16_t year = (uint16_t)(1980 + (fdate >> 9));
    uint16_t month = (uint16_t)((fdate >> 5) & 0x0F);
    uint16_t day = (uint16_t)(fdate & 0x1F);
    uint16_t hour = (uint16_t)(ftime >> 11);
    uint16_t minute = (uint16_t)((ftime >> 5) & 0x3F);

    con_print_u32(year, 10);
    con_putc('-');
    print_2d(month);
    con_putc('-');
    print_2d(day);
    con_putc(' ');
    print_2d(hour);
    con_putc(':');
    print_2d(minute);
}

static int _cdecl ls_visitor(const fat_dirent_t *e, void *ctx) {
    const char *display_name = (e->lfn[0] != '\0') ? e->lfn : e->name;
    uint16_t pad;
    (void)ctx;

    if (e->attr & FAT_ATTR_DIRECTORY) {
        con_puts("  <DIR>  ");
    } else {
        con_puts("        ");
    }
    con_puts(display_name);

    pad = (uint16_t)str_len(display_name);
    while (pad < 13) {
        con_putc(' ');
        pad++;
    }

    if (!(e->attr & FAT_ATTR_DIRECTORY)) {
        con_print_u32(e->size, 10);
        con_puts(" bytes");
    } else {
        con_puts("      ");
    }

    con_puts("  ");
    print_fat_datetime(e->wrt_date, e->wrt_time);
    con_puts("\r\n");
    return 0;
}

static void _cdecl cmd_ls(void) {
    fat_list_cwd(ls_visitor, 0);
}

static void _cdecl cmd_pwd(void) {
    char buf[13 * 8 + 2];
    fat_get_cwd(buf);
    con_puts(buf);
    con_puts("\r\n");
}

static void _cdecl cmd_cd(const char *arg) {
    const char *err;
    if (arg[0] == '\0') {
        con_puts("usage: cd <directory>\r\n");
        return;
    }
    if (!fat_change_dir(arg, &err)) {
        con_puts("cd: ");
        con_puts(err);
        con_puts("\r\n");
    }
}

static void _cdecl cat_sink(const uint8_t *data, uint16_t len, void *ctx) {
    uint16_t i;
    (void)ctx;
    for (i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            con_putc('\r');
        }
        con_putc(c);
    }
}

static void _cdecl cmd_cat(const char *arg) {
    fat_dirent_t entry;
    const char *err;

    if (arg[0] == '\0') {
        con_puts("usage: cat <file>\r\n");
        return;
    }
    if (!fat_find_file(arg, &entry, &err)) {
        con_puts("cat: ");
        con_puts(err);
        con_puts("\r\n");
        return;
    }
    if (!fat_read_file(&entry, cat_sink, 0)) {
        con_puts("cat: read error\r\n");
        return;
    }
    con_puts("\r\n");
}

static void _cdecl cmd_mkdir(const char *arg) {
    const char *err;
    if (arg[0] == '\0') {
        con_puts("usage: mkdir <directory>\r\n");
        return;
    }
    if (!fat_mkdir(arg, &err)) {
        con_puts("mkdir: ");
        con_puts(err);
        con_puts("\r\n");
    }
}

static void _cdecl cmd_touch(const char *arg) {
    const char *err;
    if (arg[0] == '\0') {
        con_puts("usage: touch <file>\r\n");
        return;
    }
    if (!fat_create_file(arg, &err)) {
        con_puts("touch: ");
        con_puts(err);
        con_puts("\r\n");
    }
}

static void _cdecl cmd_rm(const char *arg) {
    const char *err;
    if (arg[0] == '\0') {
        con_puts("usage: rm <file|directory>\r\n");
        return;
    }
    if (!fat_remove(arg, &err)) {
        con_puts("rm: ");
        con_puts(err);
        con_puts("\r\n");
    }
}

/* cp/mv buffer the whole source file in this static array (there is no
 * heap). 32KB comfortably fits the small-model 64KB data segment
 * alongside g_fat_buf (4.5KB) and the rest of the kernel's globals, while
 * still covering any file that fits on this project's 1.44MB floppy many
 * times over. Files larger than this are rejected with a clear error
 * instead of overflowing the buffer. */
#define CP_MAX_SIZE 32768UL
static uint8_t g_cp_buf[CP_MAX_SIZE];

typedef struct {
    uint32_t len;
    int overflow;
} cp_sink_ctx_t;

static void _cdecl cp_sink(const uint8_t *data, uint16_t len, void *ctx) {
    cp_sink_ctx_t *c = (cp_sink_ctx_t *)ctx;
    uint16_t i;
    for (i = 0; i < len; i++) {
        if (c->len >= CP_MAX_SIZE) {
            c->overflow = 1;
            return;
        }
        g_cp_buf[c->len++] = data[i];
    }
}

/* Shared by cp and mv: reads src fully into g_cp_buf, then (over)writes
 * dst with that data. Returns 1 on success; on failure *err_msg is set
 * and dst is left untouched (fat_write_file only touches disk once the
 * whole source has been read into memory). */
static int _cdecl do_copy(const char *src, const char *dst, const char **err_msg) {
    fat_dirent_t entry;
    cp_sink_ctx_t ctx;

    if (!fat_find_file(src, &entry, err_msg)) {
        return 0;
    }
    if (entry.size > CP_MAX_SIZE) {
        *err_msg = "file too large to copy (max 32KB)";
        return 0;
    }

    ctx.len = 0;
    ctx.overflow = 0;
    if (!fat_read_file(&entry, cp_sink, &ctx)) {
        *err_msg = "read error";
        return 0;
    }
    if (ctx.overflow) {
        *err_msg = "file too large to copy (max 32KB)";
        return 0;
    }

    return fat_write_file(dst, g_cp_buf, ctx.len, 0, err_msg);
}

static void _cdecl cmd_cp(const char *src, const char *dst) {
    const char *err;
    if (src[0] == '\0' || dst[0] == '\0') {
        con_puts("usage: cp <src> <dst>\r\n");
        return;
    }
    if (!do_copy(src, dst, &err)) {
        con_puts("cp: ");
        con_puts(err);
        con_puts("\r\n");
    }
}

static void _cdecl cmd_mv(const char *src, const char *dst) {
    const char *err;
    if (src[0] == '\0' || dst[0] == '\0') {
        con_puts("usage: mv <src> <dst>\r\n");
        return;
    }
    if (str_cmp(src, dst) == 0) {
        /* Same path: nothing to do, and critically must not fall through
         * to removing src below (that would delete the only copy). */
        return;
    }
    if (!do_copy(src, dst, &err)) {
        con_puts("mv: ");
        con_puts(err);
        con_puts("\r\n");
        return;
    }
    if (!fat_remove(src, &err)) {
        con_puts("mv: copied but could not remove source: ");
        con_puts(err);
        con_puts("\r\n");
    }
}

/* Prints bytes as a short human-readable size (e.g. "1.4M", "512 bytes"),
 * using only integer arithmetic (no floating point is available here). */
static void _cdecl print_human_size(uint32_t bytes) {
    char numbuf[12];
    uint32_t unit;
    char suffix;

    if (bytes >= 1024UL * 1024UL) {
        unit = 1024UL * 1024UL;
        suffix = 'M';
    } else if (bytes >= 1024UL) {
        unit = 1024UL;
        suffix = 'K';
    } else {
        con_print_u32(bytes, 10);
        con_puts(" bytes");
        return;
    }

    u32_to_str(bytes / unit, numbuf, 10);
    con_puts(numbuf);
    con_putc('.');
    u32_to_str(((bytes % unit) * 10) / unit, numbuf, 10);
    con_puts(numbuf);
    con_putc(suffix);
}

static void _cdecl cmd_df(void) {
    uint32_t total, free_space;

    fat_get_space(&total, &free_space);

    con_print_u32(total, 10);
    con_puts(" bytes total, ");
    con_print_u32(free_space, 10);
    con_puts(" bytes free (");
    print_human_size(total);
    con_puts(" total, ");
    print_human_size(free_space);
    con_puts(" free)\r\n");
}

/* args is the remainder of the line after "write"/"append" (from
 * rest_after_first_token); splits it in place into a filename and the
 * text to write. */
static void _cdecl cmd_write_or_append(char *args, int append) {
    char *filename = args;
    char *text;
    const char *err;

    while (*filename == ' ') {
        filename++;
    }
    if (*filename == '\0') {
        con_puts(append ? "usage: append <file> <text>\r\n" : "usage: write <file> <text>\r\n");
        return;
    }

    text = filename;
    while (*text != '\0' && *text != ' ') {
        text++;
    }
    if (*text == ' ') {
        *text++ = '\0';
    }
    while (*text == ' ') {
        text++;
    }

    if (!fat_write_file(filename, (const uint8_t *)text, (uint32_t)str_len(text), append, &err)) {
        con_puts(append ? "append: " : "write: ");
        con_puts(err);
        con_puts("\r\n");
    }
}

static void _cdecl cmd_calc(const char *arg) {
    int32_t result;
    const char *err;

    if (arg[0] == '\0') {
        con_puts("usage: calc <expression>  (e.g. calc 2 + 3 * 4)\r\n");
        return;
    }
    if (!calc_eval(arg, &result, &err)) {
        con_puts("calc: ");
        con_puts(err);
        con_puts("\r\n");
        return;
    }
    con_print_i32(result);
    con_puts("\r\n");
}

static void _cdecl cmd_help(void) {
    con_puts("Available commands:\r\n");
    con_puts("  help            show this help\r\n");
    con_puts("  ls              list files in the current directory\r\n");
    con_puts("  cd <dir>        change directory (supports .. and /abs/paths)\r\n");
    con_puts("  pwd             print the current directory\r\n");
    con_puts("  cat <file>      print a file's contents\r\n");
    con_puts("  mkdir <dir>     create a directory\r\n");
    con_puts("  touch <file>    create an empty file\r\n");
    con_puts("  write <f> <t>   write text to a file (overwrites it)\r\n");
    con_puts("  append <f> <t>  append text to a file\r\n");
    con_puts("  rm <f|dir>      remove a file or empty directory\r\n");
    con_puts("  cp <src> <dst>  copy a file (max 32KB)\r\n");
    con_puts("  mv <src> <dst>  move/rename a file\r\n");
    con_puts("  df              show filesystem space usage\r\n");
    con_puts("  calc <expr>     evaluate an arithmetic expression\r\n");
    con_puts("  echo <text>     print text back\r\n");
    con_puts("  clear           clear the screen\r\n");
    con_puts("  reboot          reboot the machine\r\n");
    con_puts("  version         print the OS version\r\n");
    con_puts("  rahma           print a special message\r\n");
}

/* Splits line in place on spaces into argv (argv[0] is the command). */
static int _cdecl tokenize(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    for (;;) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (argc < max_args) {
            argv[argc++] = p;
        }
        while (*p != '\0' && *p != ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        *p++ = '\0';
    }
    return argc;
}

/* Returns the remainder of the line after the first token, with leading
 * spaces skipped (used for commands that take a free-form argument, such
 * as calc's expression or echo's text). */
static char* _cdecl rest_after_first_token(char *line) {
    char *p = line;
    while (*p != '\0' && *p != ' ') {
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    return p;
}

void _cdecl shell_run(void) {
    char line[LINE_MAX];
    char raw[LINE_MAX];     /* unmodified copy, for commands that need the
                             * free-form remainder (calc, echo): tokenize()
                             * below overwrites spaces in `line` with NULs */
    char *argv[MAX_ARGS];
    int argc;

    con_puts("\r\nType 'help' for a list of commands.\r\n\r\n");

    for (;;) {
        char cwd[13 * 8 + 2];

        fat_get_cwd(cwd);
        con_puts("GL1TCH:");
        con_puts(cwd);
        con_puts("> ");

        read_line(line, LINE_MAX);
        str_copy(raw, line);
        argc = tokenize(line, argv, MAX_ARGS);

        if (argc == 0) {
            continue;
        }

        if (str_cmp(argv[0], "help") == 0) {
            cmd_help();
        } else if (str_cmp(argv[0], "ls") == 0) {
            cmd_ls();
        } else if (str_cmp(argv[0], "pwd") == 0) {
            cmd_pwd();
        } else if (str_cmp(argv[0], "cd") == 0) {
            cmd_cd(argc > 1 ? argv[1] : "");
        } else if (str_cmp(argv[0], "cat") == 0) {
            cmd_cat(argc > 1 ? argv[1] : "");
        } else if (str_cmp(argv[0], "mkdir") == 0) {
            cmd_mkdir(argc > 1 ? argv[1] : "");
        } else if (str_cmp(argv[0], "touch") == 0) {
            cmd_touch(argc > 1 ? argv[1] : "");
        } else if (str_cmp(argv[0], "rm") == 0) {
            cmd_rm(argc > 1 ? argv[1] : "");
        } else if (str_cmp(argv[0], "cp") == 0) {
            cmd_cp(argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "");
        } else if (str_cmp(argv[0], "mv") == 0) {
            cmd_mv(argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "");
        } else if (str_cmp(argv[0], "df") == 0) {
            cmd_df();
        } else if (str_cmp(argv[0], "write") == 0) {
            cmd_write_or_append(rest_after_first_token(raw), 0);
        } else if (str_cmp(argv[0], "append") == 0) {
            cmd_write_or_append(rest_after_first_token(raw), 1);
        } else if (str_cmp(argv[0], "calc") == 0) {
            cmd_calc(rest_after_first_token(raw));
        } else if (str_cmp(argv[0], "echo") == 0) {
            con_puts(rest_after_first_token(raw));
            con_puts("\r\n");
        } else if (str_cmp(argv[0], "clear") == 0 || str_cmp(argv[0], "cls") == 0) {
            x86_clear_screen();
        } else if (str_cmp(argv[0], "reboot") == 0) {
            x86_reboot();
        } else if (str_cmp(argv[0], "version") == 0) {
            con_puts("GL1TCH OS v1.0\r\n");
        } else if (str_cmp(argv[0], "rahma") == 0) {
            con_puts("Rahma is the best <3 <3 <3!\r\n");
            con_puts("I love you sooooooooooooooooooooooooo much <3 <3 <3 <3 <3 <3 <3 <3 <3\r\n");
        } else {
            con_puts("Unknown command: ");
            con_puts(argv[0]);
            con_puts(" (type 'help')\r\n");
        }
    }
}
