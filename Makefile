CC=gcc
CFLAGS=-Oz -Wall -std=c99 -D_GNU_SOURCE
LIBS=-lncursesw
LDFLAGS=
OBJS=$(patsubst %.c,%.o,$(wildcard *.c))
TARGET=yocto
MKDIR=mkdir -p
INSTALL=install

ifeq ($(shell uname -s),Darwin)
CFLAGS+=-DITS_OSX
LIBS=-lncurses
endif

prefix=/usr/local
bindir=$(prefix)/bin
man1dir=$(prefix)/share/man/man1
docdir=$(prefix)/share/doc/$(TARGET)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) $(TARGET) $(OBJS)

install: $(TARGET)
	$(MKDIR) $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir) $(DESTDIR)/$(docdir)
	$(INSTALL) -m 755 $(TARGET) $(DESTDIR)$(bindir)
	$(INSTALL) -m 644 doc/$(TARGET).1 $(DESTDIR)$(man1dir)
	$(INSTALL) -m 644 LICENSE $(DESTDIR)$(docdir)
	$(INSTALL) -m 644 README $(DESTDIR)$(docdir)

.PHONY: all clean
