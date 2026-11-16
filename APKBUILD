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
f3490711f2fdebe4c9200654b63a90b69ffd4dad0e9cf2fd5040a4cd57371ebb3c012a855aff9d8d2aa0a2c500baf2143b581a980177685f863ca3e4cc763851  moerx.c
e6336e712a9a6071b16cbde83cd6f591c480da68cf5185f0db27f321b26adb075844a958e792da419dcb82a930de6cff28bc64a98e9b0facd9350e382aa95ce1  Makefile
56e641534bc68b48a70d7be8337df8a191c5a2a1bafee12dc6d9ac401d107cb6b8cf8138bcc6d4f74d373689e1bb8f2ac298a87043a95f0383d6163dfc363116  moerx.1
63f842b7ee65c7bfd3feceeb27a0199f44f3e741a399ee6f59e5c78cfdde951cce88b6f368abd451d4c2efd76b6cda97be15450743031d590fc22020268fd6af  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
"
