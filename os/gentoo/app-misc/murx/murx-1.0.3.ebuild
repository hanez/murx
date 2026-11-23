EAPI=8

inherit toolchain-funcs

DESCRIPTION="Small persistent terminal multiplexer"
HOMEPAGE="https://xw3.org/murx/murx"
SRC_URI="https://xw3.org/murx/murx/archive/${P}.tar.gz -> ${P}.tar.gz"
S="${WORKDIR}/${PN}"

LICENSE="Apache-2.0"
SLOT="0"
KEYWORDS="~amd64"

DOCS=( README.md AUTHORS RELEASES.md )

src_compile() {
	tc-export CC
	emake
}

src_test() {
	./murx --help >/dev/null || die "murx --help failed"
	[[ $(./murx --version) == "${PN} ${PV}" ]] || die "Unexpected murx version"
}

src_install() {
	emake DESTDIR="${D}" PREFIX=/usr install
	einstalldocs
}
