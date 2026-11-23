# Local source snapshot; refresh version and checksums with `make pkgsums`.
pkgname=murx
pkgver=1.0.3
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
68c0e952a57d03e5c2cc6a7451cca1687341c20620634617529b6dc84f0fcdc7cb49cbc662bfad5b1fe3229c082a61480813ed5c01a1994c1b2a8e34bd421e29  murx.c
ebce5c1268253d4dbf1cc1c7d6200c3bad97cba6361d8ff3c5d4563b670565a1a70d83d7bcb2dc0b1ca9fad9ca49d2ff8366b21ac73fb26cb7b46730d0acc425  Makefile
219082e6277ee8c226cdd61a5b17d4e3c6893adf04b2f59ea84a6b6421dbd5a6d0ff38b93d8dd8ae41ed037e77252058dacadd5ff1f0c36c7c1ff95503e1e7a8  murx.1
8353d847c8d49c3f74956f17612528c1d20b20e7f981f352a2ca601ce93222c99ef3e0c034e14648df1a466376148cb1f06d3078f3a98bc1a9a00f0e3aa71c02  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
