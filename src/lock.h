#ifndef LOCK_H
#define LOCK_H

#include "shm.h"

typedef enum {
    LOCK_GLOBAL  = 0,
    LOCK_PER_ROW = 1,
    LOCK_NONE    = 2
} LockMode;

extern LockMode g_lock_mode;

int  lock_init_shared(SharedBoard *board);

void lock_row_acquire(SharedBoard *board, int row);
void lock_row_release(SharedBoard *board, int row);

void lock_range_acquire(SharedBoard *board, int r0, int r1);
void lock_range_release(SharedBoard *board, int r0, int r1);

void lock_global_acquire(SharedBoard *board);
void lock_global_release(SharedBoard *board);

void lock_notify(SharedBoard *board);

uint64_t lock_wait_change(SharedBoard *board, uint64_t last_version);

#endif /* LOCK_H */
