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
	rm -f *.apk alpine/*.apk
	rm -f *.xz arch/*.pkg.tar.*

pkgsources:
	@for dir in alpine arch; do \
	    for source in murx.c Makefile murx.1 LICENSE AUTHORS; do \
	        if [ ! -e "$$dir/$$source" ] && [ ! -L "$$dir/$$source" ]; then \
	            ln -s "../$$source" "$$dir/$$source" || exit 1; \
	        fi; \
	    done; \
	    if [ ! -e "$$dir/MURX-README.md" ] && [ ! -L "$$dir/MURX-README.md" ]; then \
	        ln -s ../README.md "$$dir/MURX-README.md" || exit 1; \
	    fi; \
	done

pkgalpine: pkgclean pkgsources
	cd alpine && abuild checksum
	cd alpine && abuild -r

pkgarch: pkgclean pkgsources
	cd arch && updpkgsums
	cd arch && makepkg

pkgsums: pkgsources
	@version=$$(sed -n 's/^#define MURX_VERSION "\([^"]*\)"$$/\1/p' murx.c); \
	if [ -z "$$version" ]; then \
	    echo "Could not read MURX_VERSION from murx.c" >&2; exit 1; \
	fi; \
	tmp=$$(mktemp .pkgsums.XXXXXX) || exit 1; \
	trap 'rm -f "$$tmp"' 0; \
	for recipe in alpine/APKBUILD arch/PKGBUILD; do \
	    sed "s/^pkgver=.*/pkgver=$$version/" "$$recipe" > "$$tmp" && \
	        cat "$$tmp" > "$$recipe" || exit 1; \
	done
	cd alpine && abuild checksum
	cd arch && updpkgsums

.PHONY: all install uninstall clean pkgclean pkgsources pkgalpine pkgarch pkgsums
