# Local source snapshot; refresh version and checksums with `make pkgsums`.
pkgname=murx
pkgver=1.0
pkgrel=1
pkgdesc='Small persistent terminal multiplexer'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/armin/murx'
license=('Apache-2.0')
depends=('glibc')
source=('murx.c' 'Makefile' 'murx.1' 'README.md' 'LICENSE' 'AUTHORS')
sha256sums=('dc7eea9afcc33209d52a0a0a9f654444ece16a122127ffae8ac7f19942521908'
            '4301fe8afe6a6e7cc665d8820ec43047f6af6f3b394a3a3a4f6e52dcf32fdec2'
            '9788331168223850709fc3c6cda9c9ab4563ff0ee4e58b992d7c2d24e02bb695'
            '4f6f36d91e4363f37d9b72b506198658619550a93ec07f122a26d009846e3a7f'
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
