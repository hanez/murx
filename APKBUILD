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
efd4dd0bcfd3ef36b1e4b8285452690f684a29e7906c0ada7aa09f879143b998a6e9cb8ddc76eb861867f882c7cb7f5a1fb350ce102e67a632d8ea238eba9b65  murx.c
4f52ecfc78bb993f9bdbaa235f671fe54327497fa8e21517805a2696bea9f06d335e4da564d0019ec1fad86986c05bce90e4825dcebc2ef3c0bc66fb9a768030  Makefile
523ac85c9b789a7721c76e8f7691a1a8ab43d7fec2624f43b3850e5135451d94404d8711fa40422a67e9f13b7388470436ab06deb6ad7c87c7e72a37492ae617  murx.1
b61f354bdd6ffbbfdf659a0359b7c9e7131230507b527e6d9cf87363e1412fdaa0bac9285bca577863146cc2dc2c3b0a55e09c1eaf63573dfacf871273a581b4  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
