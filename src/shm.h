#ifndef SHM_H
#define SHM_H

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

#define CANVAS_W      80
#define CANVAS_H      22
#define MAX_USERS     3
#define UNDO_RING_SZ  128
#define USERNAME_MAX  32
#define SHM_NAME      "/drawboard_shm"


typedef struct {
    char    ch;
    uint8_t color;   // ncurses color pair index 
} Cell;

typedef struct {
    pid_t  pid;
    char   username[USERNAME_MAX];
    int    cursor_x;
    int    cursor_y;
    uint8_t color;   /* 1-8 */
    int    active;   /* boolean */
} UserSlot;

typedef struct {
    int     x, y;
    char    old_ch;
    uint8_t old_color;
    int     user_slot;  
} UndoEntry;

typedef struct {
    volatile int      initialized;   
    volatile uint64_t version;       

    int      user_count;
    UserSlot users[MAX_USERS];

    pthread_mutex_t global_lock;
    pthread_mutex_t row_locks[CANVAS_H];
    pthread_cond_t  change_cond;

    Cell canvas[CANVAS_H][CANVAS_W];

    UndoEntry undo_buf[UNDO_RING_SZ];
    int       undo_head;   
    int       undo_count;  
} SharedBoard;

/* Open/create the shared segment; returns pointer or NULL on error */
SharedBoard *shm_open_or_create(int *created_out);

/* Attach to an existing segment (no creation) */
SharedBoard *shm_attach(void);

/* Detach the mapped region */
void shm_detach(SharedBoard *board);

/* Remove the named segment from /dev/shm */
void shm_remove(void);

/* Claim a free user slot; returns slot index (0-2) or -1 if full */
int  shm_claim_slot(SharedBoard *board, const char *username, uint8_t color);

/* Release a user slot */
void shm_release_slot(SharedBoard *board, int slot);

#endif 
