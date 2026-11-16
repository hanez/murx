# Local source snapshot; refresh checksums with `abuild checksum` after edits.
pkgname=moerx
pkgver=0.20261116
pkgrel=0
pkgdesc="Small persistent terminal multiplexer"
url="https://codeberg.org/armin/moerx"
arch="all"
license="Apache-2.0"
subpackages="$pkgname-doc"
source="moerx.c Makefile moerx.1 README.md LICENSE"
builddir="$srcdir"

build() {
    make CC="${CC:-cc}" CFLAGS="$CFLAGS -std=c99 -Wall -Wextra"
}

check() {
    ./moerx --help >/dev/null
}

package() {
    make DESTDIR="$pkgdir" PREFIX=/usr install
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

sha512sums="
cc8a8de425c3149ca7e66df304330f3ceb13851b31986afc28bdbfe92e854fbea44ca3955178e4a8e5507a04cf9eb08f6093b2f92fd3b865ccad9df58777fa8b  moerx.c
e6336e712a9a6071b16cbde83cd6f591c480da68cf5185f0db27f321b26adb075844a958e792da419dcb82a930de6cff28bc64a98e9b0facd9350e382aa95ce1  Makefile
cc677bc8c9637963dd19a031c0920f7178e71a93d236d1445ef4a5cbabefd3b3a68d9696444d2a6d0a9993ed05df55e8de876e7e515bd31effaeb7425c90a52a  moerx.1
1c062a64ce9a9578cee00eda52c0efe895a695aa8ff989250a4a1f0a84ef06ed8be7800e90942ebbe11b5feb8a464941c0084f1bd975efb038c0884278f95565  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
"
