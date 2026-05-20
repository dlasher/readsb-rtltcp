#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "threadpool.h"

static atomic_int completed_tasks;

static void test_task(void *arg, threadpool_threadbuffers_t *tbufs) {
    (void)tbufs;
    int *val = (int *)arg;
    atomic_fetch_add(&completed_tasks, 1);
    *val = *val + 1;
}

static int test_basic_dispatch(void) {
    threadpool_t *pool = threadpool_create(2, 0);
    if (!pool) {
        fprintf(stderr, "FAIL: threadpool_create returned NULL\n");
        return 0;
    }

    threadpool_task_t tasks[4];
    int results[4] = {0};

    atomic_store(&completed_tasks, 0);
    for (int i = 0; i < 4; i++) {
        tasks[i].function = test_task;
        tasks[i].argument = &results[i];
    }

    threadpool_run(pool, tasks, 4);

    if (atomic_load(&completed_tasks) != 4) {
        fprintf(stderr, "FAIL: test_basic_dispatch completed=%d expected=4\n", atomic_load(&completed_tasks));
        threadpool_destroy(pool);
        return 0;
    }

    for (int i = 0; i < 4; i++) {
        if (results[i] != 1) {
            fprintf(stderr, "FAIL: test_basic_dispatch result[%d]=%d expected=1\n", i, results[i]);
            threadpool_destroy(pool);
            return 0;
        }
    }

    threadpool_destroy(pool);
    printf("PASS: test_basic_dispatch\n");
    return 1;
}

static int test_single_task(void) {
    threadpool_t *pool = threadpool_create(2, 0);
    if (!pool) return 0;

    atomic_store(&completed_tasks, 0);
    threadpool_task_t task;
    int result = 0;

    task.function = test_task;
    task.argument = &result;

    threadpool_run(pool, &task, 1);

    if (atomic_load(&completed_tasks) != 1 || result != 1) {
        fprintf(stderr, "FAIL: test_single_task completed=%d result=%d\n", atomic_load(&completed_tasks), result);
        threadpool_destroy(pool);
        return 0;
    }

    threadpool_destroy(pool);
    printf("PASS: test_single_task\n");
    return 1;
}

static int test_many_tasks(void) {
    int n = 100;
    threadpool_t *pool = threadpool_create(4, 0);
    if (!pool) return 0;

    threadpool_task_t *tasks = calloc(n, sizeof(threadpool_task_t));
    int *results = calloc(n, sizeof(int));

    atomic_store(&completed_tasks, 0);
    for (int i = 0; i < n; i++) {
        tasks[i].function = test_task;
        tasks[i].argument = &results[i];
    }

    threadpool_run(pool, tasks, n);

    if (atomic_load(&completed_tasks) != n) {
        fprintf(stderr, "FAIL: test_many_tasks completed=%d expected=%d\n", atomic_load(&completed_tasks), n);
        threadpool_destroy(pool);
        free(tasks);
        free(results);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        if (results[i] != 1) {
            fprintf(stderr, "FAIL: test_many_tasks result[%d]=%d\n", i, results[i]);
            threadpool_destroy(pool);
            free(tasks);
            free(results);
            return 0;
        }
    }

    threadpool_destroy(pool);
    free(tasks);
    free(results);
    printf("PASS: test_many_tasks (%d tasks across 4 threads)\n", n);
    return 1;
}

int main(void) {
    int ok = 1;
    ok = test_basic_dispatch() && ok;
    ok = test_single_task() && ok;
    ok = test_many_tasks() && ok;
    return ok ? 0 : 1;
}
