CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man

# openpty(3) lives in libutil on the BSDs, but in libc on Linux and macOS
# (and in musl). Resolve it in the shell so this Makefile works with both
# GNU make and BSD make.
PTY_LIB_SH = uname -s | grep -Eq 'BSD|DragonFly' && echo -lutil

all: murx

murx: murx.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ murx.c $(LDLIBS) `$(PTY_LIB_SH)`

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

pkgalpine: pkgclean
	abuild checksum
	abuild -r

pkgarch: pkgclean
	updpkgsums
	makepkg

pkgsums:
	abuild checksum
	updpkgsums

.PHONY: all install uninstall clean pkgclean pkgalpine pkgarch pkgsums
