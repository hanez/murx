# Local source snapshot; refresh checksums with `updpkgsums` after edits.
pkgname=murx
pkgver=0.20261116
pkgrel=1
pkgdesc='Small persistent terminal multiplexer'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/armin/murx'
license=('Apache-2.0')
depends=('glibc')
source=('murx.c' 'Makefile' 'murx.1' 'README.md' 'LICENSE' 'AUTHORS')
sha256sums=('6e6b2ce665984239d753cd5683ae9469009763420ced4a0048f18788df350dd6'
            'c683ed74c664ebb2a72603297367ed102afdf27e4d6cee47920db8632a8efa91'
            '0a2be096fb83ff1a17d0194547a40a84337c0c5c43da5f0940d1aecd76f742bf'
            '476af085c9d2d54d047757b45e2e1bcf39798df500a4c98eac3d9cea727fb50f'
            '31c5556be4cdd9bf52bb55f18ead0c68a4f0e793c2c2dab0a2a2c6d92407d0d4'
            'dbc8a4fc569d4cd7bcd45516ccc98c1396fce0682c262103069c8f5150538cdb')

build() {
    cd "$srcdir"
    make CFLAGS="$CFLAGS -std=c99 -Wall -Wextra"
}

check() {
    cd "$srcdir"
    ./murx --help >/dev/null
}

package() {
    cd "$srcdir"
    make DESTDIR="$pkgdir" PREFIX=/usr install
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 AUTHORS "$pkgdir/usr/share/doc/$pkgname/AUTHORS"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
