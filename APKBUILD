# Local source snapshot; refresh checksums with `abuild checksum` after edits.
pkgname=murx
pkgver=0.20261116
pkgrel=0
pkgdesc="Small persistent terminal multiplexer"
url="https://codeberg.org/armin/murx"
arch="all"
license="Apache-2.0"
makedepends="clang"
subpackages="$pkgname-doc"
source="murx.c Makefile murx.1 README.md LICENSE AUTHORS"
builddir="$srcdir"

build() {
    make CC=clang CFLAGS="$CFLAGS -std=c99 -Wall -Wextra"
}

check() {
    ./murx --help >/dev/null
}

package() {
    make DESTDIR="$pkgdir" PREFIX=/usr install
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 AUTHORS "$pkgdir/usr/share/doc/$pkgname/AUTHORS"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

sha512sums="
f16817759ba74f46d3ab89ae0dda690c6b80febfa50e7a4593b98694e42b2db16b046ad76f1f50101a6d3a1af91df621e8c526afadf7be47aff626174fcbf999  murx.c
4f52ecfc78bb993f9bdbaa235f671fe54327497fa8e21517805a2696bea9f06d335e4da564d0019ec1fad86986c05bce90e4825dcebc2ef3c0bc66fb9a768030  Makefile
523ac85c9b789a7721c76e8f7691a1a8ab43d7fec2624f43b3850e5135451d94404d8711fa40422a67e9f13b7388470436ab06deb6ad7c87c7e72a37492ae617  murx.1
78ba1633f449019542df121a876f2af1b0dff0277adbd9e1d0a23b3e396a8cb66bf0ff4509bf26753ad00e541d66a57ac53ce80bbebfd15c551952c2f74caddf  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
