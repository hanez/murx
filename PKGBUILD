# Local source snapshot; refresh checksums with `updpkgsums` after edits.
pkgname=moerx
pkgver=0.20261116
pkgrel=1
pkgdesc='Small persistent terminal multiplexer'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/armin/moerx'
license=('Apache-2.0')
depends=('glibc')
source=('moerx.c' 'Makefile' 'moerx.1' 'README.md' 'LICENSE')
sha256sums=(
    '6269f68e371de276308b25e4b3d3acadcd2eaec0ba86537571d2cf80ade5e97b'
    'ad08bd0acd671f1f1716250528c2d4e5acc78a83bf55fcebdf41fa101fbb71d5'
    '38b60956c86513c53bd129cb3541b28ac161c17d56c58441e8904cba7ea501d8'
    '03a41aae42949ea1ed840d56ff66e0de0d0b1e54f1a457709ba2bf84ffaee33b'
    '31c5556be4cdd9bf52bb55f18ead0c68a4f0e793c2c2dab0a2a2c6d92407d0d4'
)

build() {
    cd "$srcdir"
    make CC="${CC:-cc}" CFLAGS="$CFLAGS -std=c99 -Wall -Wextra"
}

check() {
    cd "$srcdir"
    ./moerx --help >/dev/null
}

package() {
    cd "$srcdir"
    make DESTDIR="$pkgdir" PREFIX=/usr install
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
