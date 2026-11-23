# Local source snapshot; refresh version and checksums with `make pkgsums`.
pkgname=murx
pkgver=1.0
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
5bfcb844833775aa348915f106b96f59174e39ba141b8e7a1efac86fbbbf27f1662d0b845c7d26bb08ff9175ccc7242fc32f8eca3041248cf3179c2d53402348  murx.c
ebce5c1268253d4dbf1cc1c7d6200c3bad97cba6361d8ff3c5d4563b670565a1a70d83d7bcb2dc0b1ca9fad9ca49d2ff8366b21ac73fb26cb7b46730d0acc425  Makefile
ba098e621d6aaa976cfbdbb4e892c6af4814f75c0f15bc0c59410d8be5b427721dfef17e5e17139b61c1268afa8c49a6bae857669bc1b0f115a0126daec45785  murx.1
cf40c708e82d02eb7a1ce95a19c37df66d3cc44ea76168e0290382c49372af309022cc73d971624d95341e46ebb4aba543372a34821b1b6aedd93aa4e667050d  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
