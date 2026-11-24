#define _GNU_SOURCE
#include "shm.h"
#include "lock.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static SharedBoard *s_board = NULL;
static size_t       s_size  = 0;

SharedBoard *shm_open_or_create(int *created_out)
{
    s_size = sizeof(SharedBoard);
    *created_out = 0;

    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd >= 0) {

        if (ftruncate(fd, (off_t)s_size) < 0) {
            perror("ftruncate");
            close(fd);
            shm_unlink(SHM_NAME);
            return NULL;
        }
        s_board = mmap(NULL, s_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
        close(fd);
        if (s_board == MAP_FAILED) {
            perror("mmap");
            shm_unlink(SHM_NAME);
            return NULL;
        }
        memset(s_board, 0, s_size);

        for (int r = 0; r < CANVAS_H; r++)
            for (int c = 0; c < CANVAS_W; c++) {
                s_board->canvas[r][c].ch    = ' ';
                s_board->canvas[r][c].color = 0;
            }

        if (lock_init_shared(s_board) != 0) {
            munmap(s_board, s_size);
            shm_unlink(SHM_NAME);
            return NULL;
        }

        __atomic_store_n(&s_board->initialized, 1, __ATOMIC_RELEASE);
        *created_out = 1;
        return s_board;
    }

    if (errno != EEXIST) {
        perror("shm_open (create)");
        return NULL;
    }

    return shm_attach();
}

SharedBoard *shm_attach(void)
{
    s_size = sizeof(SharedBoard);

    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd < 0) {
        perror("shm_open (attach)");
        return NULL;
    }

    s_board = mmap(NULL, s_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (s_board == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    int retries = 0;
    while (!__atomic_load_n(&s_board->initialized, __ATOMIC_ACQUIRE)) {
        usleep(1000);
        if (++retries > 2000) {
            fprintf(stderr, "Timed out waiting for segment initialization\n");
            munmap(s_board, s_size);
            return NULL;
        }
    }
    return s_board;
}

void shm_detach(SharedBoard *board)
{
    if (board)
        munmap(board, s_size);
    s_board = NULL;
}

void shm_remove(void)
{
    shm_unlink(SHM_NAME);
}

int shm_claim_slot(SharedBoard *board, const char *username, uint8_t color)
{
    lock_global_acquire(board);

    int slot = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (!board->users[i].active) {
            slot = i;
            break;
        }
    }

    if (slot >= 0) {
        board->users[slot].pid = getpid();
        board->users[slot].cursor_x = 0;
        board->users[slot].cursor_y = 0;
        board->users[slot].color = color;
        board->users[slot].active = 1;
        strncpy(board->users[slot].username, username, USERNAME_MAX - 1);
        board->users[slot].username[USERNAME_MAX - 1] = '\0';
        board->user_count++;
    }

    lock_global_release(board);
    return slot;
}

void shm_release_slot(SharedBoard *board, int slot)
{
    lock_global_acquire(board);
    memset(&board->users[slot], 0, sizeof(UserSlot));
    if (board->user_count > 0)
        board->user_count--;
    lock_global_release(board);
}
