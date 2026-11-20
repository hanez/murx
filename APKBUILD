# Local source snapshot; refresh checksums with `abuild checksum` after edits.
pkgname=murx
pkgver=0.20261116
pkgrel=0
pkgdesc="Small persistent terminal multiplexer"
url="https://codeberg.org/armin/murx"
arch="all"
license="Apache-2.0"
subpackages="$pkgname-doc"
source="murx.c Makefile murx.1 README.md LICENSE AUTHORS"
builddir="$srcdir"

build() {
    make CFLAGS="$CFLAGS -std=c99 -Wall -Wextra"
}

check() {
    ./murx --help >/dev/null
}

package() {
    make DESTDIR="$pkgdir" PREFIX=/usr install
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 AUTHORS "$pkgdir/usr/share/doc/$pkgname/AUTHORS"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

sha512sums="
a2ce74a762c5d4a29ed2a0c1db930c4d6c5a02ff1b75c87c1a713ed3db01f26356d4a9bc73b43da7a94e05e90421353c7db3e5765a219b05adfda5690917c683  murx.c
ca73b902d64fc9dc52d8d14b6c8ca6b494f4bea3f3602c29fd2909252668e110edff075c881e014e689d7c7d990dee2a4a96208276bae50243ca382adc5ead80  Makefile
e084b9d2eaf2d93080898439a98e9a0a9acf2e076a53849f89ea0fff9a24d277cb348736cd3607bf18c9b94bd72af1c60d37617af9347d0452a04aff7160bedb  murx.1
af70fd707b1bcaaac68fffe8090783e06c9d494cc2ad11c0bfcc8e963bd51a658a02b17e296ed4017b87ffd04a412389c04d65a3192f78908deb7d70645162c1  README.md
2fe33ba22c5f68e124904be594208f050be20f33d115f9a56f60cac93304102da32e6a7e78154e831d3704da7ca8bfefd7025e7b8901e65babcd83e7342f6382  LICENSE
4d32dba475e5f9818fcd008068b011cbae9fc4b8e29897c933775d6660f44a7992d08a1550320d83233af729a42b241ef1d182bf5983ef2f64deab76cab2260d  AUTHORS
"
