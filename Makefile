CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -std=c11
LDFLAGS = -lncurses -lrt -pthread
TARGET  = board
SRCS    = src/main.c src/shm.c src/lock.c src/canvas.c src/ui.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -O2 -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -O2 -Isrc -c $< -o $@

debug: CFLAGS += -g -fsanitize=address,undefined -O0
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
