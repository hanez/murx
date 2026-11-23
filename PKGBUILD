# Local source snapshot; refresh version and checksums with `make pkgsums`.
pkgname=murx
pkgver=1.0.1
pkgrel=1
pkgdesc='Small persistent terminal multiplexer'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/armin/murx'
license=('Apache-2.0')
depends=('glibc')
source=('murx.c' 'Makefile' 'murx.1' 'README.md' 'LICENSE' 'AUTHORS')
sha256sums=('b807f06d82023878160e5a5234fff53f60bc2e9a18b71f449693cd72264666bd'
            '4301fe8afe6a6e7cc665d8820ec43047f6af6f3b394a3a3a4f6e52dcf32fdec2'
            'fbe804fcc53a3b3e261fcf7381fe90dddfb5035b4182fbe92013a921b297f8bc'
            '3c636ad59432991d29591fe956565ff53ddb78751f8c448b18a042f537102983'
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
