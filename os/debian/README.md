# Build the Debian package

Install `build-essential`, `dpkg-dev`, and `debhelper` with compatibility
level 13 available. From the murx checkout root, run:

```sh
make pkgdebian
```

The target temporarily links `os/debian` at the checkout root for
`dpkg-buildpackage` and builds an unsigned binary package. Find its version
in the [Debian changelog](changelog). The `.deb` is written to the checkout's
parent directory. Install the package with `apt install` and the path to the
`.deb`.
