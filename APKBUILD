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
ae9e049ba65f70bac3acc6c37d0b40b4ce52593b687672a507b753ad3ca55ef02e0d0bcb1a2310982edab9c1ec77b0439fcadbd9762ccecb3143cb22ebe97562  moerx.c
e6336e712a9a6071b16cbde83cd6f591c480da68cf5185f0db27f321b26adb075844a958e792da419dcb82a930de6cff28bc64a98e9b0facd9350e382aa95ce1  Makefile
1158a716d81fc33b850bf1fbb2e2ca821297da173d5024bd88418fc0f82273f9889bebe6ace5b8d9d09c21f833f96a56e5d7cb58f9b96c827b57b2e02640f7c2  moerx.1
e801f1c949bd4a8f4420660787587ca844cc5be739925e2ea8195bc3a5ec8185c7c2517e1e10c2f6f34aac65f2bbd08ac86be07b732d499a97f1987bfda4a7c6  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
"
