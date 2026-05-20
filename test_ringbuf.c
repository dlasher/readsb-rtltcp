#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Ring buffer — eliminates memmove shifts in net_io.c send queue.
// head = next write position, tail = next read position.
// buffer is full when (head - tail) == size.
// Uses power-of-2 size for fast modulo via mask.

typedef struct {
    char *buf;
    int size;
    int mask;
    int head;  // write position (wraps)
    int tail;  // read position (wraps)
} ringbuf_t;

static void ringbuf_init(ringbuf_t *rb, int size) {
    // size must be power of 2
    rb->size = size;
    rb->mask = size - 1;
    rb->buf = (char *)calloc(1, size);
    rb->head = 0;
    rb->tail = 0;
}

static void ringbuf_destroy(ringbuf_t *rb) {
    free(rb->buf);
    rb->buf = NULL;
}

static int ringbuf_used(ringbuf_t *rb) {
    return rb->head - rb->tail;
}

static int ringbuf_avail(ringbuf_t *rb) {
    return rb->size - ringbuf_used(rb);
}

// Write len bytes from src into the ring buffer.
// Returns bytes written (may be < len if full).
static int ringbuf_write(ringbuf_t *rb, const char *src, int len) {
    int avail = ringbuf_avail(rb);
    if (avail < len) len = avail;
    if (len <= 0) return 0;

    // First chunk: from head to end of buffer
    int head_off = rb->head & rb->mask;
    int first = rb->size - head_off;
    if (first > len) first = len;
    memcpy(rb->buf + head_off, src, first);

    // Second chunk: wrap around to start
    if (first < len) {
        memcpy(rb->buf, src + first, len - first);
    }

    rb->head += len;
    return len;
}

// Read/consume len bytes from the ring buffer into dst.
// Returns bytes read (may be < len if empty).
static int ringbuf_read(ringbuf_t *rb, char *dst, int len) {
    int used = ringbuf_used(rb);
    if (used < len) len = used;
    if (len <= 0) return 0;

    int tail_off = rb->tail & rb->mask;
    int first = rb->size - tail_off;
    if (first > len) first = len;
    memcpy(dst, rb->buf + tail_off, first);

    if (first < len) {
        memcpy(dst + first, rb->buf, len - first);
    }

    rb->tail += len;
    return len;
}

// Discard n bytes from the ring buffer (advance tail).
static void ringbuf_discard(ringbuf_t *rb, int n) {
    int used = ringbuf_used(rb);
    if (n > used) n = used;
    rb->tail += n;
}

// Get pointer to contiguous readable data.
// Returns length of contiguous data at tail.
static int ringbuf_contiguous_read(ringbuf_t *rb, char **out) {
    int tail_off = rb->tail & rb->mask;
    *out = rb->buf + tail_off;
    int cont = rb->size - tail_off;
    int used = ringbuf_used(rb);
    if (cont > used) cont = used;
    return cont;
}

// --- Tests ---

static int test_basic_write_read(void) {
    ringbuf_t rb;
    ringbuf_init(&rb, 16);

    ringbuf_write(&rb, "ABCD", 4);
    if (ringbuf_used(&rb) != 4) {
        printf("FAIL: test_basic_write_read used=%d\n", ringbuf_used(&rb));
        ringbuf_destroy(&rb);
        return 0;
    }

    char out[16] = {0};
    ringbuf_read(&rb, out, 4);
    if (memcmp(out, "ABCD", 4) != 0) {
        printf("FAIL: test_basic_write_read data=%s\n", out);
        ringbuf_destroy(&rb);
        return 0;
    }

    if (ringbuf_used(&rb) != 0) {
        printf("FAIL: test_basic_write_read not empty after read\n");
        ringbuf_destroy(&rb);
        return 0;
    }

    ringbuf_destroy(&rb);
    printf("PASS: test_basic_write_read\n");
    return 1;
}

static int test_wrap_around(void) {
    ringbuf_t rb;
    ringbuf_init(&rb, 8);

    // Write 6, discard 4, write 4, read all.
    // After discard: 2 bytes "AA" at position 4-5
    // After write "BBBB": 2 bytes at 6-7 ("BB"), 2 bytes wrap to 0-1 ("BB")
    // Read all: "AABBBB"
    ringbuf_write(&rb, "AAAAAA", 6);
    ringbuf_discard(&rb, 4);
    if (ringbuf_used(&rb) != 2) {
        printf("FAIL: test_wrap_around used=%d after discard\n", ringbuf_used(&rb));
        ringbuf_destroy(&rb);
        return 0;
    }

    ringbuf_write(&rb, "BBBB", 4);
    if (ringbuf_used(&rb) != 6) {
        printf("FAIL: test_wrap_around used=%d after write\n", ringbuf_used(&rb));
        ringbuf_destroy(&rb);
        return 0;
    }

    char out[16] = {0};
    int n = ringbuf_read(&rb, out, 6);
    if (n != 6 || memcmp(out, "AABBBB", 6) != 0) {
        printf("FAIL: test_wrap_around data=%.6s (expected AABBBB, read %d)\n", out, n);
        ringbuf_destroy(&rb);
        return 0;
    }

    ringbuf_destroy(&rb);
    printf("PASS: test_wrap_around\n");
    return 1;
}

static int test_full_buffer(void) {
    ringbuf_t rb;
    ringbuf_init(&rb, 4);

    int w = ringbuf_write(&rb, "ABCD", 4);
    if (w != 4) {
        printf("FAIL: test_full_buffer write returned %d\n", w);
        ringbuf_destroy(&rb);
        return 0;
    }

    // Buffer is full, further writes should return 0
    w = ringbuf_write(&rb, "E", 1);
    if (w != 0) {
        printf("FAIL: test_full_buffer write of extra byte returned %d\n", w);
        ringbuf_destroy(&rb);
        return 0;
    }

    ringbuf_destroy(&rb);
    printf("PASS: test_full_buffer\n");
    return 1;
}

static int test_contiguous_after_wrap(void) {
    ringbuf_t rb;
    ringbuf_init(&rb, 8);

    // Write 6, discard 5 — head=6, tail=5, used=1 byte "A" at pos 5
    ringbuf_write(&rb, "AAAAAA", 6);
    ringbuf_discard(&rb, 5);

    // Write 4 more — head=10, tail=5
    // 2 bytes at pos 6-7, 2 bytes wrap to pos 0-1
    ringbuf_write(&rb, "BBBB", 4);

    // Verify contiguous read at tail=5: 3 bytes (pos 5,6,7 = "ABB")
    char *cont;
    int c = ringbuf_contiguous_read(&rb, &cont);
    if (c != 3) {
        printf("FAIL: test_contiguous_after_wrap cont=%d expected 3\n", c);
        ringbuf_destroy(&rb);
        return 0;
    }
    if (memcmp(cont, "ABB", 3) != 0) {
        printf("FAIL: test_contiguous_after_wrap data=%.3s expected ABB\n", cont);
        ringbuf_destroy(&rb);
        return 0;
    }

    // Discard 3, now tail=8 (masked=0), used=2 bytes (pos 0-1 = "BB")
    ringbuf_discard(&rb, 3);
    c = ringbuf_contiguous_read(&rb, &cont);
    if (c != 2) {
        printf("FAIL: test_contiguous_after_wrap cont2=%d expected 2\n", c);
        ringbuf_destroy(&rb);
        return 0;
    }

    ringbuf_destroy(&rb);
    printf("PASS: test_contiguous_after_wrap\n");
    return 1;
}

static int test_empty_read(void) {
    ringbuf_t rb;
    ringbuf_init(&rb, 8);

    char out[4] = {0};
    int r = ringbuf_read(&rb, out, 4);
    if (r != 0) {
        printf("FAIL: test_empty_read returned %d\n", r);
        ringbuf_destroy(&rb);
        return 0;
    }

    ringbuf_destroy(&rb);
    printf("PASS: test_empty_read\n");
    return 1;
}

int main(void) {
    int ok = 1;
    ok = test_basic_write_read() && ok;
    ok = test_wrap_around() && ok;
    ok = test_full_buffer() && ok;
    ok = test_contiguous_after_wrap() && ok;
    ok = test_empty_read() && ok;
    return ok ? 0 : 1;
}
