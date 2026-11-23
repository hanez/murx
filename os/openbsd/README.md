# Build the OpenBSD package

Copy the [`sysutils/murx` port](sysutils/murx/) into the `sysutils` category
of a OpenBSD ports tree. From the copied port directory, run:

```sh
make package
```

The ports framework fetches the tagged source archive, verifies its checksum,
and builds the package. Run `make install` from the port directory to install it through the ports tree.

To package a new release, update `DISTNAME` in the port Makefile and run
`make makesum` after publishing the matching tag.
