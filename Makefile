CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin

all: moerx

moerx: moerx.c
	$(CC) $(CFLAGS) -o $@ moerx.c

install: moerx
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 moerx "$(DESTDIR)$(BINDIR)/moerx"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/moerx"

clean:
	rm -f moerx

.PHONY: all install uninstall clean
