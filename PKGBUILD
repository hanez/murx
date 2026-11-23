# Local source snapshot; refresh version and checksums with `make pkgsums`.
pkgname=murx
pkgver=1.0.2
pkgrel=1
pkgdesc='Small persistent terminal multiplexer'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/armin/murx'
license=('Apache-2.0')
depends=('glibc')
source=('murx.c' 'Makefile' 'murx.1' 'README.md' 'LICENSE' 'AUTHORS')
sha256sums=('a13edd7ca2da2d431ecd9e8fa1a4cae0a6e99a6a074893df204a73e8b3b19fc0'
            '4301fe8afe6a6e7cc665d8820ec43047f6af6f3b394a3a3a4f6e52dcf32fdec2'
            '953966bfdd0b49cb26c11220e1e2bf6b5862aaa1b2d0bf002cde9d276f0ae96b'
            '6411f0a1129e0436fd2b86372ac58adb6336eb4b7eacff268650d5188626e8ff'
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
