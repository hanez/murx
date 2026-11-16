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
    '85c51d8bcfb660287ea1cc2dbb8a0edd6ab137ce766a929ddb4f0ae0e7e37d32'
    'ad08bd0acd671f1f1716250528c2d4e5acc78a83bf55fcebdf41fa101fbb71d5'
    '5787b0b3f36e5369511d165bb7097492888d39a35a936b1a2e2d47760156d6cf'
    'be0803b92b688e125133cc62f5df31bb925203e88d8f31749fb7b5ebcdb4c71c'
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
