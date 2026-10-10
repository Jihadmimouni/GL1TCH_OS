#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "kstring.h"
#include "calc.h"
#include "fat.h"
#include "x86.h"

/* Bumped from 128 so a few chained commands (";"/"&&") still fit on one
 * line. Still a small fixed buffer - no dynamic allocation here. */
#define LINE_MAX 192
#define MAX_ARGS 8

/* Bounded scratch buffer for `>`/`>>`/`|`: holds whatever a captured
 * command would otherwise have printed to the console. Sized generously
 * enough for a directory listing or small text file, but still a fixed
 * static buffer - printing more than this to a redirected/piped command
 * fails cleanly instead of overflowing. */
#define CAP_MAX 512

/* Set by dispatch()/cmd_xxx() to report whether the last command run
 * failed (unknown command, missing/bad argument, or an underlying fat_*
 * call failing) - used to implement `&&`. Commands are still plain void
 * functions; this flag is the least invasive way to get a pass/fail
 * signal out of them without changing every call site's signature. */
static int g_last_cmd_failed = 0;

/* Reused scratch buffers for the scripting layer below. Static (not
 * stack-local) on purpose: the real-mode kernel's stack is only 512
 * bytes (see linker.lnk), so buffers that don't need to survive a
 * function return live here instead of growing shell_run()'s frame. */
static char g_line[LINE_MAX];       /* the line read from the keyboard */
static char g_tok_buf[LINE_MAX];    /* tokenizer's mutable working copy */
static char g_combined[LINE_MAX];   /* "<rhs> <piped text>" for `|` */
static char g_capture_buf[CAP_MAX]; /* raw captured console output */
static char g_pipe_text[CAP_MAX];   /* NUL-terminated copy of the above,
                                      * for building g_combined */

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
        g_last_cmd_failed = 1;
        return;
    }
    if (!fat_change_dir(arg, &err)) {
        con_puts("cd: ");
        con_puts(err);
        con_puts("\r\n");
        g_last_cmd_failed = 1;
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
        g_last_cmd_failed = 1;
        return;
    }
    if (!fat_find_file(arg, &entry, &err)) {
        con_puts("cat: ");
        con_puts(err);
        con_puts("\r\n");
        g_last_cmd_failed = 1;
        return;
    }
    if (!fat_read_file(&entry, cat_sink, 0)) {
        con_puts("cat: read error\r\n");
        g_last_cmd_failed = 1;
        return;
    }
    con_puts("\r\n");
}

static void _cdecl cmd_mkdir(const char *arg) {
    const char *err;
    if (arg[0] == '\0') {
        con_puts("usage: mkdir <directory>\r\n");
        g_last_cmd_failed = 1;
        return;
    }
    if (!fat_mkdir(arg, &err)) {
        con_puts("mkdir: ");
        con_puts(err);
        con_puts("\r\n");
        g_last_cmd_failed = 1;
    }
}

static void _cdecl cmd_touch(const char *arg) {
    const char *err;
    if (arg[0] == '\0') {
        con_puts("usage: touch <file>\r\n");
        g_last_cmd_failed = 1;
        return;
    }
    if (!fat_create_file(arg, &err)) {
        con_puts("touch: ");
        con_puts(err);
        con_puts("\r\n");
        g_last_cmd_failed = 1;
    }
}

static void _cdecl cmd_rm(const char *arg) {
    const char *err;
    if (arg[0] == '\0') {
        con_puts("usage: rm <file|directory>\r\n");
        g_last_cmd_failed = 1;
        return;
    }
    if (!fat_remove(arg, &err)) {
        con_puts("rm: ");
        con_puts(err);
        con_puts("\r\n");
        g_last_cmd_failed = 1;
    }
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
        g_last_cmd_failed = 1;
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
        g_last_cmd_failed = 1;
    }
}

static void _cdecl cmd_calc(const char *arg) {
    int32_t result;
    const char *err;

    if (arg[0] == '\0') {
        con_puts("usage: calc <expression>  (e.g. calc 2 + 3 * 4)\r\n");
        g_last_cmd_failed = 1;
        return;
    }
    if (!calc_eval(arg, &result, &err)) {
        con_puts("calc: ");
        con_puts(err);
        con_puts("\r\n");
        g_last_cmd_failed = 1;
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
    con_puts("  calc <expr>     evaluate an arithmetic expression\r\n");
    con_puts("  echo <text>     print text back\r\n");
    con_puts("  clear           clear the screen\r\n");
    con_puts("  reboot          reboot the machine\r\n");
    con_puts("  version         print the OS version\r\n");
    con_puts("  rahma           print a special message\r\n");
    con_puts("\r\nScripting:\r\n");
    con_puts("  a ; b           run a, then always run b\r\n");
    con_puts("  a && b          run b only if a succeeded\r\n");
    con_puts("  cmd > file      write cmd's output to file (overwrite)\r\n");
    con_puts("  cmd >> file     append cmd's output to file\r\n");
    con_puts("  a | b           feed a's output to b as its arguments\r\n");
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

/*
 * Tokenizes tokline (destructively, see tokenize()) and runs whichever
 * command it names, setting g_last_cmd_failed. raw must be an
 * unmodified copy of the same text (same contents as tokline before
 * tokenizing it) - commands with a free-form argument (calc/echo/
 * write/append) read their argument out of raw via
 * rest_after_first_token() instead of argv, since tokenize() would
 * otherwise have chopped it up on spaces.
 */
static void _cdecl dispatch(char *tokline, char *raw) {
    char *argv[MAX_ARGS];
    int argc;

    g_last_cmd_failed = 0;
    argc = tokenize(tokline, argv, MAX_ARGS);

    if (argc == 0) {
        return;
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
        g_last_cmd_failed = 1;
    }
}

/*
 * Copies raw (a single command, with its original spacing intact) into
 * the tokenizer scratch buffer and dispatches it. raw is left
 * unmodified by this - only g_tok_buf is chopped up by tokenize().
 */
static void _cdecl run_plain(char *raw) {
    str_copy(g_tok_buf, raw); /* always fits: raw is a substring of a
                                * line already bounded by LINE_MAX, and
                                * g_tok_buf is LINE_MAX too */
    dispatch(g_tok_buf, raw);
}

/*
 * Runs raw with its console output captured instead of displayed, then
 * writes whatever it printed to target (appending if append is set).
 * Used for `>`/`>>`.
 */
static void _cdecl run_captured(char *raw, char *target, int append) {
    uint16_t len;
    const char *err;

    con_capture_start(g_capture_buf, CAP_MAX);
    run_plain(raw);
    len = con_capture_stop();

    if (con_capture_overflowed()) {
        con_puts("shell: output too large to redirect\r\n");
        g_last_cmd_failed = 1;
        return;
    }

    if (!fat_write_file(target, (const uint8_t *)g_capture_buf, (uint32_t)len, append, &err)) {
        con_puts("shell: ");
        con_puts(err);
        con_puts("\r\n");
        g_last_cmd_failed = 1;
    }
}

/*
 * Runs left_raw with its output captured, then runs
 * "<right_raw> <captured output>" as a brand new command line (so e.g.
 * "echo hi | write out.txt" ends up running "write out.txt hi"). If
 * target is non-NULL, that second command's output is redirected to it
 * instead of displayed (for "a | b > file").
 */
static void _cdecl run_piped(char *left_raw, char *right_raw, char *target, int append) {
    uint16_t len;
    uint16_t i;
    uint16_t n;

    con_capture_start(g_capture_buf, CAP_MAX);
    run_plain(left_raw);
    len = con_capture_stop();

    if (con_capture_overflowed()) {
        con_puts("shell: piped output too large\r\n");
        g_last_cmd_failed = 1;
        return;
    }

    n = (uint16_t)(len < CAP_MAX - 1 ? len : CAP_MAX - 1);
    mem_copy(g_pipe_text, g_capture_buf, n);
    g_pipe_text[n] = '\0';

    n = 0;
    for (i = 0; right_raw[i] != '\0' && n < LINE_MAX - 1; i++) {
        g_combined[n++] = right_raw[i];
    }
    if (n < LINE_MAX - 1) {
        g_combined[n++] = ' ';
    }
    for (i = 0; g_pipe_text[i] != '\0' && n < LINE_MAX - 1; i++) {
        g_combined[n++] = g_pipe_text[i];
    }
    g_combined[n] = '\0';

    if (target != 0) {
        run_captured(g_combined, target, append);
    } else {
        run_plain(g_combined);
    }
}

static int _cdecl is_blank(const char *s) {
    while (*s == ' ') {
        s++;
    }
    return *s == '\0';
}

/*
 * Parses and runs a single ";"/"&&"-delimited command: strips off a
 * trailing `>`/`>>`/redirection target and/or a `|` pipe (if present),
 * then hands the remaining command text to run_plain/run_captured/
 * run_piped. seg is modified in place (NULs inserted where `>`/`|`
 * were found) - that is fine, it is never read again afterwards.
 */
static void _cdecl execute_segment(char *seg) {
    char *target = 0;
    int append = 0;
    char *pipe_right = 0;
    char *gt;
    char *bar;

    /* Leading spaces are common here (e.g. the " echo two" left after
     * splitting "a ; echo two" on ';'), but rest_after_first_token()
     * (used by echo/calc/write/append) assumes its input starts right
     * at the command name with no leading space - so strip it now,
     * same as the top-level line always implicitly had. */
    while (*seg == ' ') {
        seg++;
    }
    gt = seg;

    /* find a top-level '>' (there is no quoting in this shell, so
     * "top-level" just means "anywhere in the text") */
    while (*gt != '\0' && *gt != '>') {
        gt++;
    }
    if (*gt == '>') {
        char *t;
        if (gt[1] == '>') {
            append = 1;
            t = gt + 2;
        } else {
            append = 0;
            t = gt + 1;
        }
        *gt = '\0';
        while (*t == ' ') {
            t++;
        }
        {
            char *end = t + str_len(t);
            while (end > t && *(end - 1) == ' ') {
                end--;
            }
            *end = '\0';
        }
        if (*t == '\0') {
            con_puts("shell: missing filename after '>'/'>>'\r\n");
            g_last_cmd_failed = 1;
            return;
        }
        target = t;
    }

    /* find a top-level '|' in whatever command text is left */
    bar = seg;
    while (*bar != '\0' && *bar != '|') {
        bar++;
    }
    if (*bar == '|') {
        char *end = bar;
        *bar = '\0';
        pipe_right = bar + 1;
        while (*pipe_right == ' ') {
            pipe_right++;
        }
        while (end > seg && *(end - 1) == ' ') {
            end--;
        }
        *end = '\0';
        if (*pipe_right == '\0') {
            con_puts("shell: missing command after '|'\r\n");
            g_last_cmd_failed = 1;
            return;
        }
    }

    if (is_blank(seg)) {
        con_puts("shell: missing command before '>'/'|'\r\n");
        g_last_cmd_failed = 1;
        return;
    }

    if (pipe_right != 0) {
        run_piped(seg, pipe_right, target, append);
    } else if (target != 0) {
        run_captured(seg, target, append);
    } else {
        run_plain(seg);
    }
}

#define OP_NONE 0
#define OP_SEMI 1
#define OP_AND  2

/* Finds the next top-level ';' or "&&" in p. Returns 0 if there is
 * none; otherwise returns a pointer to it and sets *op. */
static char* _cdecl find_next_op(char *p, int *op) {
    while (*p != '\0') {
        if (*p == ';') {
            *op = OP_SEMI;
            return p;
        }
        if (*p == '&' && p[1] == '&') {
            *op = OP_AND;
            return p;
        }
        p++;
    }
    return 0;
}

/*
 * Splits line on top-level ';' and '&&' and runs each piece through
 * execute_segment() in order. ';' always runs the next piece; '&&'
 * only runs it if the previous piece (that actually ran) succeeded.
 * A piece skipped because of a failed '&&' counts as failed itself,
 * so "a && b && c" stops at the first failure.
 */
static void _cdecl run_script_line(char *line) {
    char *cursor = line;
    int pending_op = OP_NONE;
    int prev_ok = 1;

    for (;;) {
        int found_op = OP_NONE;
        char *sep = find_next_op(cursor, &found_op);
        char *seg = cursor;
        char saved = '\0';
        int has_more = (sep != 0);
        int this_op = pending_op;
        int run_this;

        if (has_more) {
            saved = *sep;
            *sep = '\0';
        }

        run_this = (this_op != OP_AND) || prev_ok;

        if (run_this) {
            if (is_blank(seg)) {
                prev_ok = 1; /* empty command, e.g. from "a ;; b" */
            } else {
                execute_segment(seg);
                prev_ok = !g_last_cmd_failed;
            }
        } else {
            prev_ok = 0; /* skipped: propagate failure through further && */
        }

        if (!has_more) {
            break;
        }
        *sep = saved;
        cursor = sep + (found_op == OP_AND ? 2 : 1);
        pending_op = found_op;
    }
}

void _cdecl shell_run(void) {
    con_puts("\r\nType 'help' for a list of commands.\r\n\r\n");

    for (;;) {
        char cwd[13 * 8 + 2];

        fat_get_cwd(cwd);
        con_puts("GL1TCH:");
        con_puts(cwd);
        con_puts("> ");

        read_line(g_line, LINE_MAX);
        run_script_line(g_line);
    }
}
