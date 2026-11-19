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
6a0b08780bb7b6a743535ce93dd648fb534a59274dcc1a0008a64737d7f8d7af2a56ac385ab3f6c74b836df93819d3b6d1c190a0074442a2e7c4e02621c6c25d  murx.c
4e2fa284eb0589d0b955dc309c6a734ca56f93a8de4542c78948d17430722e2b0f6d5f2dc4fb2ce79c1a26ba50233af04384b665860f937a1d49e4eaf4d42d48  Makefile
ea99904e2e3b02c1a2f3bd8e1f2faef9aeb3cd8bc97c2ab14ce2d66de7d16a804bbf9610ade09385fd2a660493372cf335faae2cc30ccec8b62d019ad95ae457  murx.1
6874ff4290c258c93f5bf5d4c7b11ab4bb1237c0dcd758f3c144989181d6cac9a738eb6cedfa6b9935ad47c8bf523226a1c0040de96608509dcb7097c4afda49  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
