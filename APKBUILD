# Local source snapshot; refresh checksums with `abuild checksum` after edits.
pkgname=murx
pkgver=0.20261116
pkgrel=0
pkgdesc="Small persistent terminal multiplexer"
url="https://codeberg.org/armin/murx"
arch="all"
license="Apache-2.0"
subpackages="$pkgname-doc"
source="murx.c Makefile murx.1 README.md LICENSE AUTHORS"
builddir="$srcdir"

build() {
    make CFLAGS="$CFLAGS -std=c99 -Wall -Wextra"
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
cca1f9816de1cdaa97c2543d74ec52fec4813c9068d756f8f940c82b58c7b3afd631a20991c981b3bc75a968d1d3d9ed36182150e32a6a7718863045643c4264  murx.c
ca73b902d64fc9dc52d8d14b6c8ca6b494f4bea3f3602c29fd2909252668e110edff075c881e014e689d7c7d990dee2a4a96208276bae50243ca382adc5ead80  Makefile
7e2535df251ac8d4a64dc8ad379337798f10afb4d0ab1642e5d010128344b626d03f08363c280cdfdb512d666a62a96fad8f4f6a2be53ae2b9bd5c13bdf0491c  murx.1
9a3bb80832e106746bd55bb1f0ffa2ee4ce7e7b141b734971756742fa28d34e310cd41867a661aef01b6b1e43903749422f135aeb97e70bc3e884d7dfe2fb0e9  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
