# Local source snapshot; refresh checksums with `updpkgsums` after edits.
pkgname=murx
pkgver=0.20261116
pkgrel=1
pkgdesc='Small persistent terminal multiplexer'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/armin/murx'
license=('Apache-2.0')
depends=('glibc')
makedepends=('clang')
source=('murx.c' 'Makefile' 'murx.1' 'README.md' 'LICENSE' 'AUTHORS')
sha256sums=('0267b3d6754fa08737da0d8b1baf175d962d7e3dfee66cb3fe0f818301280deb'
            '1d77a97ff77ce6d876baef98e22b06cfa2109a4ae65cef8b75a91df4c02ad8ac'
            'f07830b93f78674956a17b108738d52c3d3efd58f431dc29f137cc339d625ee5'
            '6e28ec8408619cba0341f148507c4a421733dea12a0ce6dea4e9d0eea9b7c65c'
            '31c5556be4cdd9bf52bb55f18ead0c68a4f0e793c2c2dab0a2a2c6d92407d0d4'
            'dbc8a4fc569d4cd7bcd45516ccc98c1396fce0682c262103069c8f5150538cdb')

build() {
    cd "$srcdir"
    make CC=clang CFLAGS="$CFLAGS -std=c99 -Wall -Wextra"
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
