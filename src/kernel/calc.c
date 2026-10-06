#include "calc.h"
#include "kstring.h"

/*
 * Recursive-descent evaluator:
 *   expr   := term (('+' | '-') term)*
 *   term   := factor (('*' | '/') factor)*
 *   factor := ('+' | '-') factor | '(' expr ')' | number
 */

static const char *g_cur;
static int g_error;
static const char *g_errmsg;

static void skip_ws(void) {
    while (is_space(*g_cur)) {
        g_cur++;
    }
}

static int32_t parse_expr(void);

static int32_t parse_factor(void) {
    int32_t v;

    skip_ws();

    if (*g_cur == '+') {
        g_cur++;
        return parse_factor();
    }
    if (*g_cur == '-') {
        g_cur++;
        return -parse_factor();
    }
    if (*g_cur == '(') {
        g_cur++;
        v = parse_expr();
        skip_ws();
        if (*g_cur == ')') {
            g_cur++;
        } else if (!g_error) {
            g_error = 1;
            g_errmsg = "missing closing parenthesis";
        }
        return v;
    }
    if (is_digit(*g_cur)) {
        if (!str_to_i32(&g_cur, &v)) {
            g_error = 1;
            g_errmsg = "invalid number";
            return 0;
        }
        return v;
    }

    g_error = 1;
    g_errmsg = "expected a number or '('";
    return 0;
}

static int32_t parse_term(void) {
    int32_t v = parse_factor();

    for (;;) {
        skip_ws();
        if (*g_cur == '*') {
            g_cur++;
            v = v * parse_factor();
        } else if (*g_cur == '/') {
            int32_t rhs;
            g_cur++;
            rhs = parse_factor();
            if (rhs == 0) {
                if (!g_error) {
                    g_error = 1;
                    g_errmsg = "division by zero";
                }
                return 0;
            }
            v = v / rhs;
        } else {
            return v;
        }
    }
}

static int32_t parse_expr(void) {
    int32_t v = parse_term();

    for (;;) {
        skip_ws();
        if (*g_cur == '+') {
            g_cur++;
            v = v + parse_term();
        } else if (*g_cur == '-') {
            g_cur++;
            v = v - parse_term();
        } else {
            return v;
        }
    }
}

int _cdecl calc_eval(const char *expr, int32_t *out, const char **err_msg) {
    int32_t v;

    g_cur = expr;
    g_error = 0;
    g_errmsg = 0;

    v = parse_expr();
    skip_ws();

    if (!g_error && *g_cur != '\0') {
        g_error = 1;
        g_errmsg = "unexpected trailing characters";
    }

    if (g_error) {
        if (err_msg != 0) {
            *err_msg = g_errmsg;
        }
        return 0;
    }

    *out = v;
    return 1;
}
