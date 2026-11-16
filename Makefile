CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man

all: moerx

moerx: moerx.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ moerx.c $(LDLIBS)

install: moerx moerx.1
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(MANDIR)/man1"
	install -m 755 moerx "$(DESTDIR)$(BINDIR)/moerx"
	install -m 644 moerx.1 "$(DESTDIR)$(MANDIR)/man1/moerx.1"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/moerx"
	rm -f "$(DESTDIR)$(MANDIR)/man1/moerx.1"

clean:
	rm -f moerx

.PHONY: all install uninstall clean
