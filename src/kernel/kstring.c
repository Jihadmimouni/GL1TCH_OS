#include "kstring.h"

void _cdecl mem_set(void *dst, uint8_t value, uint16_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint16_t i;
    for (i = 0; i < n; i++) {
        d[i] = value;
    }
}

void _cdecl mem_copy(void *dst, const void *src, uint16_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint16_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

uint16_t _cdecl str_len(const char *s) {
    uint16_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int _cdecl str_cmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int _cdecl str_ncmp(const char *a, const char *b, uint16_t n) {
    uint16_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0') {
            return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
        }
    }
    return 0;
}

char _cdecl to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

int _cdecl str_icmp(const char *a, const char *b) {
    while (*a != '\0' && to_upper(*a) == to_upper(*b)) {
        a++;
        b++;
    }
    return (int)(uint8_t)to_upper(*a) - (int)(uint8_t)to_upper(*b);
}

void _cdecl str_copy(char *dst, const char *src) {
    while (*src != '\0') {
        *dst++ = *src++;
    }
    *dst = '\0';
}

char* _cdecl str_chr(const char *s, char c) {
    while (*s != '\0') {
        if (*s == c) {
            return (char *)s;
        }
        s++;
    }
    return 0;
}

int _cdecl is_space(char c) {
    return c == ' ' || c == '\t';
}

int _cdecl is_digit(char c) {
    return c >= '0' && c <= '9';
}

void _cdecl u32_to_str(uint32_t v, char *out, uint8_t base) {
    char tmp[11];
    int i = 0;
    int j = 0;

    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }

    while (v > 0) {
        uint32_t rem = v % base;
        v = v / base;
        tmp[i++] = (char)((rem < 10) ? ('0' + rem) : ('A' + rem - 10));
    }

    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

void _cdecl i32_to_str(int32_t v, char *out) {
    uint32_t mag;

    if (v < 0) {
        *out++ = '-';
        mag = (uint32_t)(-v);
    } else {
        mag = (uint32_t)v;
    }
    u32_to_str(mag, out, 10);
}

int _cdecl str_to_i32(const char **s, int32_t *out) {
    const char *p = *s;
    int32_t v = 0;
    int any = 0;

    while (is_digit(*p)) {
        v = v * 10 + (*p - '0');
        p++;
        any = 1;
    }

    if (!any) {
        return 0;
    }

    *out = v;
    *s = p;
    return 1;
}
