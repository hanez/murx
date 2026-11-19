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
8eefff81f546d9893d2878b6208fceaf36d6c66ec6a9b1627545f81802f82cfbf9edef7fb3310aa516a682eb775c68afaabd294f3d5ca8463a7b0329801ac87c  murx.c
ca73b902d64fc9dc52d8d14b6c8ca6b494f4bea3f3602c29fd2909252668e110edff075c881e014e689d7c7d990dee2a4a96208276bae50243ca382adc5ead80  Makefile
ea99904e2e3b02c1a2f3bd8e1f2faef9aeb3cd8bc97c2ab14ce2d66de7d16a804bbf9610ade09385fd2a660493372cf335faae2cc30ccec8b62d019ad95ae457  murx.1
b30835225a564919f4a6043f0348e4d16886179bb523715c4b7cd5b75fb2490cc649aeffa4cac19d3db90409dd5ca222b31f704f1c5f116bf86b21b3a95f54d6  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
