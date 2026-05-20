#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

// Model of receiver.c's hash table.
// Uses a mutex for concurrent safety (same pattern we'll add to receiver.c).
// Without the mutex, concurrent creates race on linked-list head updates.

#define TABLE_HASH_BITS 4  // small table = more collisions
#define TABLE_SIZE (1 << TABLE_HASH_BITS)

typedef struct entry {
    uint64_t id;
    struct entry *next;
} entry_t;

static entry_t *table[TABLE_SIZE];
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned int fast_seed(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (unsigned)(ts.tv_sec ^ ts.tv_nsec ^ (uintptr_t)pthread_self());
}

static uint32_t hash_id(uint64_t id) {
    uint64_t h = id * 0x9e3779b97f4a7c15ULL;
    h ^= (h >> 30);
    return (uint32_t)(h & (TABLE_SIZE - 1));
}

static entry_t *locked_lookup(uint64_t id) {
    entry_t *e = table[hash_id(id)];
    while (e && e->id != id)
        e = e->next;
    return e;
}

static entry_t *locked_create(uint64_t id) {
    entry_t *e = locked_lookup(id);
    if (e) return e;

    uint32_t h = hash_id(id);
    e = (entry_t *)calloc(1, sizeof(entry_t));
    e->id = id;
    e->next = table[h];
    table[h] = e;
    return e;
}

typedef struct {
    int thread_id;
    int ops;
    int errors;
} worker_arg_t;

static void *worker_with_lock(void *arg) {
    worker_arg_t *a = (worker_arg_t *)arg;
    unsigned int seed = fast_seed() ^ (a->thread_id << 16);

    for (int i = 0; i < a->ops; i++) {
        uint64_t id = (uint64_t)(rand_r(&seed) & 0xffff);

        pthread_mutex_lock(&table_lock);
        entry_t *e = locked_create(id);
        if (e) {
            if (e->id != id) {
                a->errors++;
            }
        }
        pthread_mutex_unlock(&table_lock);
    }
    return NULL;
}

static int test_concurrent_safe(void) {
    memset(table, 0, sizeof(table));

    int n_threads = 8;
    int ops_per_thread = 20000;
    pthread_t threads[n_threads];
    worker_arg_t args[n_threads];

    for (int i = 0; i < n_threads; i++) {
        args[i].thread_id = i;
        args[i].ops = ops_per_thread;
        args[i].errors = 0;
        pthread_create(&threads[i], NULL, worker_with_lock, &args[i]);
    }

    int total_errors = 0;
    for (int i = 0; i < n_threads; i++) {
        pthread_join(threads[i], NULL);
        total_errors += args[i].errors;
    }

    if (total_errors > 0) {
        fprintf(stderr, "FAIL: test_concurrent_safe — %d data corruptions with lock!\n", total_errors);
        return 0;
    }

    printf("PASS: test_concurrent_safe (%d threads, %d ops each, mutex-protected)\n",
           n_threads, ops_per_thread);
    return 1;
}

static int test_single_create_lookup(void) {
    memset(table, 0, sizeof(table));
    pthread_mutex_lock(&table_lock);
    entry_t *e = locked_create(0x1234);
    pthread_mutex_unlock(&table_lock);
    if (!e) {
        printf("FAIL: test_single_create_lookup create\n");
        return 0;
    }

    pthread_mutex_lock(&table_lock);
    e = locked_lookup(0x1234);
    pthread_mutex_unlock(&table_lock);
    if (!e || e->id != 0x1234) {
        printf("FAIL: test_single_create_lookup lookup\n");
        return 0;
    }

    printf("PASS: test_single_create_lookup\n");
    return 1;
}

static int test_missing_lookup(void) {
    memset(table, 0, sizeof(table));
    pthread_mutex_lock(&table_lock);
    entry_t *e = locked_lookup(0xFFFFFFFFFFFFFFFFULL);
    pthread_mutex_unlock(&table_lock);
    if (e) {
        printf("FAIL: test_missing_lookup\n");
        return 0;
    }
    printf("PASS: test_missing_lookup\n");
    return 1;
}

int main(void) {
    int ok = 1;
    ok = test_single_create_lookup() && ok;
    ok = test_missing_lookup() && ok;
    ok = test_concurrent_safe() && ok;
    return ok ? 0 : 1;
}
