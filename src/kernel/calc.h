#pragma once
#include "stdint.h"

/*
 * Evaluates a simple arithmetic expression (+, -, *, /, parentheses,
 * unary minus, integer operands). Returns 1 on success with *out set,
 * or 0 with *err_msg set to a short description of the problem.
 */
int _cdecl calc_eval(const char *expr, int32_t *out, const char **err_msg);
