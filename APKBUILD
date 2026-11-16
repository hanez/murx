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
efc748dd85e0fda80baaf3b047f8b5a8d57c6231973540be2da9383a8aad8ae4c08a9a1665e219bcd4b4e045d2a88d271d2cde934b1023cf165096c188ea8da4  moerx.c
e6336e712a9a6071b16cbde83cd6f591c480da68cf5185f0db27f321b26adb075844a958e792da419dcb82a930de6cff28bc64a98e9b0facd9350e382aa95ce1  Makefile
06aa39542f0842cfddf7c14dda7fcff739c58a6c21ee899be77f9c18fcd9575fc6061d6846318153a2703747cbce814a9f87e63b6e905f529a42af91ddb98470  moerx.1
feaee93a326f0b62b1b655ef8b776c470a49feaaab41d6814ca9dd1d6f3416b70f65deda08e9362dc0e28f347123fa84a84e2f2c727566fb702003fed2caa68c  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
"
