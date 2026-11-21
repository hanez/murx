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
0a297bc9295533c76b9e4b271b0f4472bf6f36caca68a2480bb0a54d435972d5f205ff9cd272c7f9038dfe59e87840f8a342e636d6f31ce8659b13f05da1ef35  murx.c
ca73b902d64fc9dc52d8d14b6c8ca6b494f4bea3f3602c29fd2909252668e110edff075c881e014e689d7c7d990dee2a4a96208276bae50243ca382adc5ead80  Makefile
db9fe919bac2b4976efa6d760b1f917ec6fee88a4f2811ea4ac69334f6f24d522569c2cd3c3d087eb43cace659bf4c00f3c4097cc901d769b4e4000342d65c45  murx.1
e7393bfcbf2a10d7251c0ae5f9fdc18534b7989bb6688fa71d0cd52eaea9db5744413963acdbefc66951d246b3f2db061168507b8bda2c516f992b9ce5ee0f72  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
