#ifndef UI_H
#define UI_H

#include "shm.h"

/* Client-local state passed between threads */
typedef struct {
    SharedBoard *board;
    int          slot;        /* user slot index */
    volatile int running;     /* set to 0 to stop threads */
    volatile int needs_redraw;/* set by signal handler / listener thread */
} ClientState;

/* Initialize ncurses, check terminal size; returns 0 on success */
int  ui_init(void);

/* Tear down ncurses and restore the terminal */
void ui_cleanup(void);

/* Full redraw: canvas + status bar + all cursors */
void ui_redraw(SharedBoard *board, int my_slot);

/* Entry points for the two threads */
void *thread_input(void *arg);    /* arg = ClientState* */
void *thread_listener(void *arg); /* arg = ClientState* */

#endif /* UI_H */
