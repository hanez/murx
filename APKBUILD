# Local source snapshot; refresh version and checksums with `make pkgsums`.
pkgname=murx
pkgver=1.0.1
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
983e2ab3d7670a4d22b5b799741e2ad6d3801b2563c1b1d77cc7233e4b9aa0646dff9122dbc0b382162f2d44ae4ca8948a3221bf5eb31a974064a5dfd4f1268d  murx.c
ebce5c1268253d4dbf1cc1c7d6200c3bad97cba6361d8ff3c5d4563b670565a1a70d83d7bcb2dc0b1ca9fad9ca49d2ff8366b21ac73fb26cb7b46730d0acc425  Makefile
ba098e621d6aaa976cfbdbb4e892c6af4814f75c0f15bc0c59410d8be5b427721dfef17e5e17139b61c1268afa8c49a6bae857669bc1b0f115a0126daec45785  murx.1
4d207bfbff88293b8864a508eab169c4fb9c1e358efdeac4820bdfdb11cc7254dc8d0f50e60ac88e6d3803b39fd6e8a740309753a1d74db8d6634c702cc2b80b  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
