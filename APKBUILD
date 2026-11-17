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
86f7e6ac508fd46badde9ad6bba7a31e2cc896d43c0265b8014561ea4f8bea4dd76fa5ebc0df9fef53ae456feb4f5cdc808ee8b7a31ad8f5849c2436fa82687b  murx.c
4f52ecfc78bb993f9bdbaa235f671fe54327497fa8e21517805a2696bea9f06d335e4da564d0019ec1fad86986c05bce90e4825dcebc2ef3c0bc66fb9a768030  Makefile
523ac85c9b789a7721c76e8f7691a1a8ab43d7fec2624f43b3850e5135451d94404d8711fa40422a67e9f13b7388470436ab06deb6ad7c87c7e72a37492ae617  murx.1
dd4c8734c16b5d454fe638db8ed38d09337e58f89634be5d552cd72757820922207fe36f87ca77087303e8a36dedc269b27620d4c5fb0052f474e1c5892fd38a  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
