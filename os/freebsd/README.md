# Build the FreeBSD package

Copy the [`sysutils/murx` port](sysutils/murx/) into the `sysutils` category
of a FreeBSD ports tree. From the copied port directory, run:

```sh
make package
```

The ports framework fetches the tagged source archive, verifies its checksum,
and builds the package. Install the resulting package with `pkg add` and the package path.

To package a new release, update `DISTVERSION` in the port Makefile and run
`make makesum` after publishing the matching tag.
