# Build the Gentoo package

Copy the [`app-misc/murx` directory](app-misc/murx/) into a configured local
Portage overlay. Keep the ebuild and Manifest together. The ebuild fetches and
verifies the tagged release archive and builds it from source.

On an amd64 system using stable keywords, add `app-misc/murx ~amd64` to
`/etc/portage/package.accept_keywords` or a file in that directory. Then run:

```sh
emerge --ask app-misc/murx
```

To prepare a new package version, copy the ebuild to a filename with the new
version and run `ebuild murx-VERSION.ebuild manifest` from the package
directory after publishing the matching tag.
