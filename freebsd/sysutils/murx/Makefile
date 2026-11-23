PORTNAME=	murx
DISTVERSION=	1.0.3
CATEGORIES=	sysutils
MASTER_SITES=	https://xw3.org/murx/murx/archive/

MAINTAINER=	you@hanez.org
COMMENT=	Small persistent terminal multiplexer
WWW=		https://codeberg.org/armin/murx

LICENSE=	APACHE20
LICENSE_FILE=	${WRKSRC}/LICENSE

WRKSRC=		${WRKDIR}/murx
MAKE_ARGS=	PREFIX="${PREFIX}" MANDIR="${PREFIX}/share/man"

PLIST_FILES=	bin/murx share/man/man1/murx.1.gz
PORTDOCS=	AUTHORS README.md RELEASES.md

OPTIONS_DEFINE=	DOCS

post-install:
	${STRIP_CMD} ${STAGEDIR}${PREFIX}/bin/murx

post-install-DOCS-on:
	${MKDIR} ${STAGEDIR}${DOCSDIR}
	${INSTALL_DATA} ${PORTDOCS:S|^|${WRKSRC}/|} ${STAGEDIR}${DOCSDIR}

.include <bsd.port.mk>
