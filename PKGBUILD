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
    '58b815779cc50ff0aeb41909d611309d1084b6c335dc35a54e79656e3d45cd51'
    'ad08bd0acd671f1f1716250528c2d4e5acc78a83bf55fcebdf41fa101fbb71d5'
    '56ef9511287758cd32e3c5cf777d7b9d99c470e1cf6570f1a94dcfd2f649b3e6'
    '4a4d3158fb5288b93bcfd0ceec76b6b4d83fdb683e2a2a05afa63a0804ba9311'
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
