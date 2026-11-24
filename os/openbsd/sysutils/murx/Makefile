COMMENT =	small persistent terminal multiplexer

DISTNAME =	murx-1.0.4
CATEGORIES =	sysutils
HOMEPAGE =	https://codeberg.org/armin/murx
MAINTAINER =	Johannes Findeisen <you@hanez.org>

# Apache-2.0
PERMIT_PACKAGE =	Yes

WANTLIB +=	c util

SITES =		https://xw3.org/murx/murx/archive/
WRKDIST =	${WRKDIR}/murx

MAKE_FLAGS =	CC="${CC}" CFLAGS="${CFLAGS} -std=c99 -Wall -Wextra" \
		LDFLAGS="${LDFLAGS}"

do-test:
	${WRKSRC}/murx --help >/dev/null

do-install:
	${INSTALL_PROGRAM_DIR} ${PREFIX}/bin
	${INSTALL_MAN_DIR} ${PREFIX}/man/man1
	${INSTALL_PROGRAM} ${WRKSRC}/murx ${PREFIX}/bin/
	${INSTALL_MAN} ${WRKSRC}/murx.1 ${PREFIX}/man/man1/
	${INSTALL_DATA_DIR} ${PREFIX}/share/doc/murx
	${INSTALL_DATA} ${WRKSRC}/AUTHORS ${WRKSRC}/README.md \
	    ${WRKSRC}/RELEASES.md ${PREFIX}/share/doc/murx/

.include <bsd.port.mk>
