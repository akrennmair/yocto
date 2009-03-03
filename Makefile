CC=gcc
LIBS=-lncurses
CFLAGS=-O2 -Wall -Wextra -std=c99
LDFLAGS=
OBJS=$(patsubst %.c,%.o,$(wildcard *.c))
TARGET=yocto
MKDIR=mkdir -p
INSTALL=install

prefix=/usr/local

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) $(TARGET) $(OBJS)

install: $(TARGET)
	$(MKDIR) $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 755 $(TARGET) $(DESTDIR)$(prefix)/bin

.PHONY: all clean
