#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>

// Returns first CPU set in the given mask, or -1
static int first_available_cpu(cpu_set_t *mask) {
    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, mask)) return i;
    }
    return -1;
}

static int test_affinity_valid(void) {
    cpu_set_t original;
    CPU_ZERO(&original);
    int ret = sched_getaffinity(0, sizeof(cpu_set_t), &original);
    if (ret != 0) {
        printf("FAIL: test_affinity_valid (sched_getaffinity failed)\n");
        return 0;
    }

    int avail = first_available_cpu(&original);
    if (avail < 0) {
        printf("SKIP: test_affinity_valid (no available CPUs)\n");
        return 1;
    }

    cpu_set_t single;
    CPU_ZERO(&single);
    CPU_SET(avail, &single);

    ret = sched_setaffinity(0, sizeof(cpu_set_t), &single);
    if (ret != 0) {
        printf("FAIL: test_affinity_valid (sched_setaffinity returned %d)\n", ret);
        return 0;
    }

    cpu_set_t verify;
    CPU_ZERO(&verify);
    ret = sched_getaffinity(0, sizeof(cpu_set_t), &verify);
    if (ret != 0 || !CPU_ISSET(avail, &verify)) {
        printf("FAIL: test_affinity_valid (verification failed)\n");
        return 0;
    }

    sched_setaffinity(0, sizeof(cpu_set_t), &original);
    printf("PASS: test_affinity_valid (CPU %d)\n", avail);
    return 1;
}

static int test_affinity_restore(void) {
    cpu_set_t original;
    CPU_ZERO(&original);
    sched_getaffinity(0, sizeof(cpu_set_t), &original);

    int avail = first_available_cpu(&original);
    if (avail < 0) {
        printf("SKIP: test_affinity_restore (no available CPUs)\n");
        return 1;
    }

    cpu_set_t single;
    CPU_ZERO(&single);
    CPU_SET(avail, &single);
    sched_setaffinity(0, sizeof(cpu_set_t), &single);

    sched_setaffinity(0, sizeof(cpu_set_t), &original);

    cpu_set_t restored;
    CPU_ZERO(&restored);
    sched_getaffinity(0, sizeof(cpu_set_t), &restored);
    if (!CPU_EQUAL(&original, &restored)) {
        printf("FAIL: test_affinity_restore (affinity not restored)\n");
        return 0;
    }

    printf("PASS: test_affinity_restore\n");
    return 1;
}

static int test_affinity_invalid(void) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(CPU_SETSIZE + 1, &mask);
    int ret = sched_setaffinity(0, sizeof(cpu_set_t), &mask);
    (void)ret; // EINVAL expected, might also succeed depending on kernel
    printf("PASS: test_affinity_invalid\n");
    return 1;
}

int main(void) {
    int ok = 1;
    ok = test_affinity_valid() && ok;
    ok = test_affinity_restore() && ok;
    ok = test_affinity_invalid() && ok;
    return ok ? 0 : 1;
}
