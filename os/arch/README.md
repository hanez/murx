# Build the Arch Linux package

Install `base-devel` and `pacman-contrib`. As a regular user, run this from
the murx checkout root:

```sh
make pkgarch
```

The target prepares local sources for [PKGBUILD](PKGBUILD), updates their
checksums, and runs `makepkg`. The package is written to `os/arch/` unless you
configured a different `PKGDEST`. Install it with `pacman -U` and the package
path.

To build and install in one step, run these commands from the checkout root:

```sh
make pkgsources
cd os/arch
updpkgsums
makepkg -si
```

The recipe supports x86_64 and aarch64.
