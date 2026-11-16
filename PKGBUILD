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
    '4418d829418d51903609b03b334cdd5e4b8e2b667d180980890ee3efae1c9f12'
    'ad08bd0acd671f1f1716250528c2d4e5acc78a83bf55fcebdf41fa101fbb71d5'
    '882d5876e386ea0539c49f1f45759f454460d36c2ede43f5ec7c0401509fc5f0'
    '000a031b54dff60d34c621c0318138b01524ba63b7d166778ccb00c2652e8d1d'
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
