# Multi-User Terminal Drawing Board

An interactive shared-canvas drawing application for the Linux terminal, built in C as an Operating Systems course project. Multiple users (up to 3) run separate processes in different terminal windows and draw on the same canvas in real time.

## OS Concepts Demonstrated

| Feature | OS Concept |
|---|---|
| `shm_open` + `mmap` | POSIX IPC — shared memory |
| `pthread_mutex` with `PTHREAD_PROCESS_SHARED` | Cross-process synchronization |
| `pthread_cond_wait` on shared condition variable | Efficient blocking instead of busy-polling |
| `PTHREAD_MUTEX_ROBUST` + `EOWNERDEAD` handling | Crash recovery / robust mutexes |
| Per-row lock ordering | Deadlock avoidance |
| `--no-lock` mode | Race condition demonstration |
| Input thread + listener/render thread per client | Multi-threading within a process |
| `SIGWINCH` / `SIGINT` / `SIGTERM` handlers | Signal handling |
| `shm_unlink` on last exit | Resource cleanup |
| Undo ring buffer in shared memory | Bounded shared data structure |

## Building

**Dependencies:** `gcc`, `libncurses-dev`, `librt` (usually bundled with glibc)

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential libncurses-dev
make
make debug
make clean
```

### Single user (quick test)

```bash
./board --user alice
```

### Multi-user demo (without tmux)

Open 3 terminal windows, `cd` to the project directory in each, then run:

**Window 1:**
```bash
./board --user alice
```

**Window 2:**
```bash
./board --user bob
```

**Window 3:**
```bash
./board --user carol
```

One person can click between the windows to play all three roles — the OS treats each as an independent process sharing the same `/dev/shm/drawboard_shm` segment.

```
### Cleanup stale segments

```bash
./board --cleanup
```

## Controls

| Key | Action |
|---|---|
| Arrow keys / WASD | Move cursor |
| Space | Toggle pen (draw `*` while moving) |
| Any printable key | Draw that character and advance cursor |
| `1`–`8` | Select color |
| `c` | Clear canvas |
| `f` | Flood fill from cursor |
| `u` | Undo last stroke |
| `q` | Quit |

## Command-line Options

```
./board [--user <name>] [--lock global|row|none] [--cleanup] [--no-lock]
```

| Flag | Description |
|---|---|
| `--user alice` | Username shown in status bar (default: `user<pid>`) |
| `--lock global` | Single global mutex (default) |
| `--lock row` | Per-row mutexes (lower contention) |
| `--lock none` / `--no-lock` | No locking — demonstrates race conditions |
| `--cleanup` | Remove stale `/dev/shm` segment and exit |

## Running a Demo (Step-by-Step Walkthrough)

1. **Start one client:** `./board --user alice`  
   Draw a few characters with arrow keys + Space or by typing letters.

2. **Start a second client** in another window: `./board --user bob`  
   Alice's drawing appears immediately in Bob's terminal. Draw in Bob's window and watch it appear in Alice's.

3. **Start a third client:** `./board --user carol`  
   Status bar in all three windows shows all three users.

4. **Crash recovery test:** While alice is drawing, in a fourth terminal run:  
   ```bash
   kill -9 <alice_pid>
   ```  
   Bob and Carol continue working. If Alice held the global mutex, the robust mutex (`PTHREAD_MUTEX_ROBUST`) returns `EOWNERDEAD` to the next locker, which calls `pthread_mutex_consistent()` to recover.

5. **Normal exit:** Press `q` in bob and carol. The last client to exit calls `shm_unlink` and removes the segment. Verify:  
   ```bash
   ls /dev/shm/ | grep drawboard
   ```  
   Should print nothing.

6. **Race condition demo:**  
   ```bash
   ./board --user alice --no-lock
   ```  
   Three clients draw rapidly with no locking. Observe corrupted characters, torn writes, and inconsistent cursor positions in the status bar.

## Architecture Overview

```
┌─────────────────────────── Process: board ──────────────────────────────┐
│                                                                          │
│  Thread A (input/draw)          Thread B (listener/render)               │
│  ─────────────────────          ──────────────────────────               │
│  getch() → move cursor          pthread_cond_wait(change_cond)           │
│  canvas_draw / clear / fill     wakes on version bump → ui_redraw()      │
│  updates users[slot].cursor     reads full canvas from shared memory     │
│                                                                          │
│         Both threads access SharedBoard via lock_row_acquire()           │
└──────────────────────────────────────────────────────────────────────────┘
              │  mmap(MAP_SHARED)
              ▼
┌──────────────── /dev/shm/drawboard_shm (SharedBoard) ──────────────────┐
│  initialized  version  user_count  users[3]                             │
│  global_lock  row_locks[24]  change_cond                                │
│  canvas[24][80]  undo_buf[128]                                          │
└─────────────────────────────────────────────────────────────────────────┘
              ▲
   (identical mapping in up to 2 more processes)
```

## File Layout

```
.
├── Makefile
├── README.md
├── src/
│   ├── main.c      Entry point, argument parsing, signal handlers, thread lifecycle
│   ├── shm.h/c     Shared memory: open/create/attach/detach, slot management
│   ├── lock.h/c    Locking layer: global, per-row, none; robust mutex wrappers
│   ├── canvas.h/c  Canvas operations: draw, clear, flood fill, undo
│   └── ui.h/c      ncurses UI: init, redraw, input thread, listener thread

```
