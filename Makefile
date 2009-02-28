CC=gcc
LIBS=-lncurses
CFLAGS=-O2 -Wall -Wextra -std=c99
LDFLAGS=
OBJS=$(patsubst %.c,%.o,$(wildcard *.c))
TARGET=me

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) $(TARGET) $(OBJS)

.PHONY: all clean
