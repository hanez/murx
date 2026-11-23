# Build the Alpine packages

Install `alpine-sdk` and configure abuild for your regular user. Generate and
install a signing key with `abuild-keygen -a -i` if you need one.

From the murx checkout root, run:

```sh
make pkgalpine
```

The target prepares local sources for [APKBUILD](APKBUILD), refreshes their
checksums, and invokes `abuild -r`. The packages are written to abuild's
configured repository destination, typically `~/packages`.

Install the resulting `murx` and `murx-doc` APKs with `apk add` and their
respective file paths. Trust your local signing key first. The `murx-doc`
subpackage contains the manual.
