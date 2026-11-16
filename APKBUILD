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
0a0cedb8702b5ac041896065c6d36d22f57deffdc3f8023c9b02b7b87218dfcd32a8e2eaecfc470930d1d1228109cf6894ba8e5242ebcf760ef2dd17ab35d2ab  moerx.c
e6336e712a9a6071b16cbde83cd6f591c480da68cf5185f0db27f321b26adb075844a958e792da419dcb82a930de6cff28bc64a98e9b0facd9350e382aa95ce1  Makefile
cc677bc8c9637963dd19a031c0920f7178e71a93d236d1445ef4a5cbabefd3b3a68d9696444d2a6d0a9993ed05df55e8de876e7e515bd31effaeb7425c90a52a  moerx.1
d1bc928e391b50a0031834396b8f655474d8073399f94e7c5dbf7469977a1ff7665db1c21ced7180bf7a891584dae61f52e1ef345ef75ab3348705aeb0d091e0  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
"
