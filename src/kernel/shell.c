#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "kstring.h"
#include "calc.h"
#include "fat.h"
#include "x86.h"

#define LINE_MAX 128
#define MAX_ARGS 8

static int _cdecl ls_visitor(const fat_dirent_t *e, void *ctx) {
    (void)ctx;
    if (e->attr & FAT_ATTR_DIRECTORY) {
        con_puts("  <DIR>  ");
    } else {
        con_puts("        ");
    }
    con_puts(e->name);
    if (!(e->attr & FAT_ATTR_DIRECTORY)) {
        uint16_t pad = (uint16_t)str_len(e->name);
        while (pad < 13) {
            con_putc(' ');
            pad++;
        }
        con_print_u32(e->size, 10);
        con_puts(" bytes");
    }
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
    con_puts("  calc <expr>     evaluate an arithmetic expression\r\n");
    con_puts("  echo <text>     print text back\r\n");
    con_puts("  clear           clear the screen\r\n");
    con_puts("  reboot          reboot the machine\r\n");
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
        } else if (str_cmp(argv[0], "calc") == 0) {
            cmd_calc(rest_after_first_token(raw));
        } else if (str_cmp(argv[0], "echo") == 0) {
            con_puts(rest_after_first_token(raw));
            con_puts("\r\n");
        } else if (str_cmp(argv[0], "clear") == 0 || str_cmp(argv[0], "cls") == 0) {
            x86_clear_screen();
        } else if (str_cmp(argv[0], "reboot") == 0) {
            x86_reboot();
        } else if (str_cmp(argv[0], "rahma") == 0) {
            con_puts("Rahma is the best <3 <3 <3!\r\n");
        }else if (str_cmp(argv[0], "exit") == 0) {
            con_puts("Exiting shell.\r\n");
            break;
        } else {
            con_puts("Unknown command: ");
            con_puts(argv[0]);
            con_puts(" (type 'help')\r\n");
        }
    }
}
