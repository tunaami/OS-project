#define _GNU_SOURCE
#include "shm.h"
#include "lock.h"
#include "canvas.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <ncurses.h>


static SharedBoard  *g_board   = NULL;
static int           g_slot    = -1;
static ClientState  *g_cs      = NULL;
static int           g_creator = 0;

static void handle_exit(int sig)
{
    (void)sig;
    if (g_cs) g_cs->running = 0;
}

static void handle_winch(int sig)
{
    (void)sig;
    if (g_cs) g_cs->needs_redraw = 1;
    endwin();
    refresh();
    clear();
}

static void cleanup(void)
{
    ui_cleanup();

    if (g_board && g_slot >= 0)
        shm_release_slot(g_board, g_slot);

    if (g_board) {
        /* Last active user removes the segment */
        int remaining = 0;
        if (g_board->initialized) {
            lock_global_acquire(g_board);
            remaining = g_board->user_count;
            lock_global_release(g_board);
        }

        shm_detach(g_board);
        g_board = NULL;

        if (g_creator && remaining == 0)
            shm_remove();
        else if (remaining == 0)
            shm_remove();
    }
}


static void do_cleanup_mode(void)
{
    shm_remove();
    printf("Removed %s from /dev/shm (if it existed)\n", SHM_NAME);

    printf("(Use scripts/cleanup.sh to also kill stray board processes.)\n");
    exit(0);
}


static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [--user <name>] [--lock global|row|none] [--cleanup]\n"
        "  --user     your username (default: user<pid>)\n"
        "  --lock     locking strategy: global (default), row, none\n"
        "  --no-lock  alias for --lock none\n"
        "  --cleanup  remove stale shared memory and exit\n",
        name);
    exit(1);
}

int main(int argc, char **argv)
{
    char username[USERNAME_MAX] = {0};
    snprintf(username, USERNAME_MAX, "user%d", (int)getpid());

    /* Default color per slot (assigned after claiming) */
    uint8_t default_colors[MAX_USERS] = {3, 2, 5}; /* green, red, blue */

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            strncpy(username, argv[++i], USERNAME_MAX - 1);
        } else if (strcmp(argv[i], "--lock") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "global") == 0)       g_lock_mode = LOCK_GLOBAL;
            else if (strcmp(argv[i], "row") == 0)     g_lock_mode = LOCK_PER_ROW;
            else if (strcmp(argv[i], "none") == 0)    g_lock_mode = LOCK_NONE;
            else { fprintf(stderr, "Unknown lock mode: %s\n", argv[i]); usage(argv[0]); }
        } else if (strcmp(argv[i], "--no-lock") == 0) {
            g_lock_mode = LOCK_NONE;
        } else if (strcmp(argv[i], "--cleanup") == 0) {
            do_cleanup_mode();
        } else {
            usage(argv[0]);
        }
    }

    /* Open or create shared memory */
    g_board = shm_open_or_create(&g_creator);
    if (!g_board) {
        fprintf(stderr, "Failed to open shared memory\n");
        return 1;
    }

    /* Claim a slot */
    /* Temporarily pick color 1; will update once we know slot */
    g_slot = shm_claim_slot(g_board, username, 1);
    if (g_slot < 0) {
        fprintf(stderr, "Board is full (max %d users). Try again later.\n", MAX_USERS);
        shm_detach(g_board);
        return 1;
    }
    /* Assign distinct default color based on slot */
    g_board->users[g_slot].color = default_colors[g_slot];

    /* Initialize ncurses */
    if (ui_init() != 0) {
        shm_release_slot(g_board, g_slot);
        shm_detach(g_board);
        return 1;
    }

    /* Set up client state */
    ClientState cs = {
        .board        = g_board,
        .slot         = g_slot,
        .running      = 1,
        .needs_redraw = 1,
    };
    g_cs = &cs;

    /* Register signal handlers */
    signal(SIGINT,  handle_exit);
    signal(SIGTERM, handle_exit);
    signal(SIGWINCH, handle_winch);

    /* Register atexit cleanup */
    atexit(cleanup);

    /* Initial draw */
    ui_redraw(g_board, g_slot);

    /* Spawn listener thread */
    pthread_t listener_tid;
    if (pthread_create(&listener_tid, NULL, thread_listener, &cs) != 0) {
        perror("pthread_create listener");
        cs.running = 0;
        return 1;
    }

    /* Run the input loop in the main thread */
    thread_input(&cs);

    /* Signal listener to stop and wait */
    cs.running = 0;
    lock_global_acquire(g_board);
    lock_notify(g_board);       /* wake the listener */
    lock_global_release(g_board);
    pthread_join(listener_tid, NULL);

    /* cleanup() called via atexit */
    return 0;
}
