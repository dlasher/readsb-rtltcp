#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Custom integer-to-string helpers to replace sprintf in hot SBS output path.
// Returns number of bytes written (excluding NUL).

static int sprint_u32_padded(char *buf, uint32_t v, int min_width) {
    char tmp[16];
    int len = 0;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        while (v) {
            tmp[len++] = '0' + (v % 10);
            v /= 10;
        }
    }
    while (len < min_width)
        tmp[len++] = '0';
    for (int i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    return len;
}

static int sprint_s32(char *buf, int32_t v) {
    if (v < 0) {
        *buf++ = '-';
        return 1 + sprint_u32_padded(buf, (uint32_t)(-(int64_t)v), 0);
    }
    return sprint_u32_padded(buf, (uint32_t)v, 0);
}

static int sprint_u32_hex(char *buf, uint32_t v, int min_width) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[16];
    int len = 0;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        while (v) {
            tmp[len++] = hex[v & 0xF];
            v >>= 4;
        }
    }
    while (len < min_width)
        tmp[len++] = '0';
    for (int i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    return len;
}

// --- Tests ---

static int test_padded_4(void) {
    char buf[16];
    int n = sprint_u32_padded(buf, 42, 4);
    buf[n] = '\0';
    if (n != 4 || strcmp(buf, "0042") != 0) {
        printf("FAIL: test_padded_4 got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_padded_4\n");
    return 1;
}

static int test_padded_2(void) {
    char buf[16];
    int n = sprint_u32_padded(buf, 5, 2);
    buf[n] = '\0';
    if (n != 2 || strcmp(buf, "05") != 0) {
        printf("FAIL: test_padded_2 got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_padded_2\n");
    return 1;
}

static int test_padded_wider(void) {
    char buf[16];
    // If value is wider than min_width, just print the value
    int n = sprint_u32_padded(buf, 12345, 2);
    buf[n] = '\0';
    if (n != 5 || strcmp(buf, "12345") != 0) {
        printf("FAIL: test_padded_wider got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_padded_wider\n");
    return 1;
}

static int test_padded_zero(void) {
    char buf[16];
    int n = sprint_u32_padded(buf, 0, 3);
    buf[n] = '\0';
    if (n != 3 || strcmp(buf, "000") != 0) {
        printf("FAIL: test_padded_zero got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_padded_zero\n");
    return 1;
}

static int test_signed_positive(void) {
    char buf[16];
    int n = sprint_s32(buf, 123);
    buf[n] = '\0';
    if (n != 3 || strcmp(buf, "123") != 0) {
        printf("FAIL: test_signed_positive got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_signed_positive\n");
    return 1;
}

static int test_signed_negative(void) {
    char buf[16];
    int n = sprint_s32(buf, -42);
    buf[n] = '\0';
    if (n != 3 || strcmp(buf, "-42") != 0) {
        printf("FAIL: test_signed_negative got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_signed_negative\n");
    return 1;
}

static int test_signed_zero(void) {
    char buf[16];
    int n = sprint_s32(buf, 0);
    buf[n] = '\0';
    if (n != 1 || strcmp(buf, "0") != 0) {
        printf("FAIL: test_signed_zero got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_signed_zero\n");
    return 1;
}

static int test_signed_int_min(void) {
    char buf[16];
    int n = sprint_s32(buf, -2147483647 - 1);
    buf[n] = '\0';
    if (n != 11 || strcmp(buf, "-2147483648") != 0) {
        printf("FAIL: test_signed_int_min got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_signed_int_min\n");
    return 1;
}

static int test_hex(void) {
    char buf[16];
    int n = sprint_u32_hex(buf, 0xFF, 2);
    buf[n] = '\0';
    if (n != 2 || strcmp(buf, "FF") != 0) {
        printf("FAIL: test_hex got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_hex\n");
    return 1;
}

static int test_hex_zero(void) {
    char buf[16];
    int n = sprint_u32_hex(buf, 0, 4);
    buf[n] = '\0';
    if (n != 4 || strcmp(buf, "0000") != 0) {
        printf("FAIL: test_hex_zero got '%s' (len=%d)\n", buf, n);
        return 0;
    }
    printf("PASS: test_hex_zero\n");
    return 1;
}

int main(void) {
    int ok = 1;
    ok = test_padded_4() && ok;
    ok = test_padded_2() && ok;
    ok = test_padded_wider() && ok;
    ok = test_padded_zero() && ok;
    ok = test_signed_positive() && ok;
    ok = test_signed_negative() && ok;
    ok = test_signed_zero() && ok;
    ok = test_signed_int_min() && ok;
    ok = test_hex() && ok;
    ok = test_hex_zero() && ok;
    return ok ? 0 : 1;
}
