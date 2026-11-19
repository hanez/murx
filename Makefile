CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man

all: murx

murx: murx.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ murx.c $(LDLIBS)

install: murx murx.1
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(MANDIR)/man1"
	install -m 755 murx "$(DESTDIR)$(BINDIR)/murx"
	install -m 644 murx.1 "$(DESTDIR)$(MANDIR)/man1/murx.1"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/murx"
	rm -f "$(DESTDIR)$(MANDIR)/man1/murx.1"

clean:
	rm -f murx

pkgclean:
	rm -rf *.apk
	rm -rf *.xz

.PHONY: all install uninstall clean
