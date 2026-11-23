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
0a40b60d95effbf69c8a0904c2031260d8398d9a0fc8f91fc45e260d655d240f6ef0cadaf0d1324b420da7031e43ed13325559d0b9026a4fba9a4eef481ecbbe  murx.c
ebce5c1268253d4dbf1cc1c7d6200c3bad97cba6361d8ff3c5d4563b670565a1a70d83d7bcb2dc0b1ca9fad9ca49d2ff8366b21ac73fb26cb7b46730d0acc425  Makefile
c79bb8fc5c00b9ed784a3221e33a9a341ad4ce5b9880585409cce8e068a1e8be1ecb77115784cca0247250870069e3d63b23833857e5e9c5ab3e8da4c036eb4f  murx.1
c8a4f7661af194b2b5d04edb90b6d4d87838711d59d12039bc8adaf84c724d311301aad0e2e1ced69719b491b910c0bbb01e610b508348cd6c860bd2c3a0723c  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
