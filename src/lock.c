#define _GNU_SOURCE
#include "lock.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

LockMode g_lock_mode = LOCK_GLOBAL;

int lock_init_shared(SharedBoard *board)
{
    pthread_mutexattr_t mattr;
    pthread_condattr_t  cattr;

    //global mutex
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
    if (pthread_mutex_init(&board->global_lock, &mattr) != 0) {
        perror("pthread_mutex_init global");
        pthread_mutexattr_destroy(&mattr);
        return -1;
    }

    //row-wise mutex
    for (int r = 0; r < CANVAS_H; r++) {
        if (pthread_mutex_init(&board->row_locks[r], &mattr) != 0) {
            perror("pthread_mutex_init row");
            pthread_mutexattr_destroy(&mattr);
            return -1;
        }
    }
    pthread_mutexattr_destroy(&mattr);

    //conditional variable
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    if (pthread_cond_init(&board->change_cond, &cattr) != 0) {
        perror("pthread_cond_init");
        pthread_condattr_destroy(&cattr);
        return -1;
    }
    pthread_condattr_destroy(&cattr);

    return 0;
}

static void robust_lock(pthread_mutex_t *m)
{
    int rc = pthread_mutex_lock(m);
    if (rc == EOWNERDEAD) {
        pthread_mutex_consistent(m);
    } else if (rc != 0) {
        fprintf(stderr, "pthread_mutex_lock error: %d\n", rc);
        exit(1);
    }
}

void lock_global_acquire(SharedBoard *board)
{
    if (g_lock_mode == LOCK_NONE) return;
    robust_lock(&board->global_lock);
}

void lock_global_release(SharedBoard *board)
{
    if (g_lock_mode == LOCK_NONE) return;
    pthread_mutex_unlock(&board->global_lock);
}

void lock_row_acquire(SharedBoard *board, int row)
{
    if (g_lock_mode == LOCK_NONE)    return;
    if (g_lock_mode == LOCK_GLOBAL) { robust_lock(&board->global_lock); return; }
    robust_lock(&board->row_locks[row]);
}

void lock_row_release(SharedBoard *board, int row)
{
    if (g_lock_mode == LOCK_NONE)    return;
    if (g_lock_mode == LOCK_GLOBAL) { pthread_mutex_unlock(&board->global_lock); return; }
    pthread_mutex_unlock(&board->row_locks[row]);
}

/* Acquire rows r0..r1 in ascending order to prevent deadlock */
void lock_range_acquire(SharedBoard *board, int r0, int r1)
{
    if (r0 > r1) { int t = r0; r0 = r1; r1 = t; }
    if (g_lock_mode == LOCK_NONE)    return;
    if (g_lock_mode == LOCK_GLOBAL) { robust_lock(&board->global_lock); return; }
    for (int r = r0; r <= r1; r++)
        robust_lock(&board->row_locks[r]);
}

void lock_range_release(SharedBoard *board, int r0, int r1)
{
    if (r0 > r1) { int t = r0; r0 = r1; r1 = t; }
    if (g_lock_mode == LOCK_NONE)    return;
    if (g_lock_mode == LOCK_GLOBAL) { pthread_mutex_unlock(&board->global_lock); return; }
    for (int r = r0; r <= r1; r++)
        pthread_mutex_unlock(&board->row_locks[r]);
}

void lock_notify(SharedBoard *board)
{
    board->version++;
    if (g_lock_mode != LOCK_NONE)
        pthread_cond_broadcast(&board->change_cond);
}

uint64_t lock_wait_change(SharedBoard *board, uint64_t last_version)
{
    if (g_lock_mode == LOCK_NONE) {
        while (board->version == last_version)
            usleep(16000);
        return board->version;
    }

    /* Must be called with global_lock already held */
    while (board->version == last_version)
        pthread_cond_wait(&board->change_cond, &board->global_lock);
    return board->version;
}
