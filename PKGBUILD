# Local source snapshot; refresh version and checksums with `make pkgsums`.
pkgname=murx
pkgver=1.0.3
pkgrel=1
pkgdesc='Small persistent terminal multiplexer'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/armin/murx'
license=('Apache-2.0')
depends=('glibc')
source=('murx.c' 'Makefile' 'murx.1' 'README.md' 'LICENSE' 'AUTHORS')
sha256sums=('358d39eed410a9657ef811892288ef407fe8f17709cb1b4b5e0405635594b70d'
            '4301fe8afe6a6e7cc665d8820ec43047f6af6f3b394a3a3a4f6e52dcf32fdec2'
            '953966bfdd0b49cb26c11220e1e2bf6b5862aaa1b2d0bf002cde9d276f0ae96b'
            'a9231e00964b19553032f531832fa532f231dcedf3d6c7c0e52a1e2761bfd6c7'
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
