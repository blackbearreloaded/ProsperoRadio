# Build output and release ZIP

Every application build creates and validates `dist/<TITLE_ID>/`. That folder is
the only build output:

```bash
make app
```

GitHub Actions also publishes `<TITLE_ID>.zip`. This is not another package
format: it is a standard ZIP containing the complete `dist/<TITLE_ID>/` folder
for users who prefer directory deployment.

## Publishing a release

A release is made by pushing the version tag: the workflow builds, attests and publishes the
ZIP and `SHA256SUMS`. Release files are not attached by hand, so that every published file
comes from a run and has an attestation. The publish step handles three cases:

- **No release for the tag:** it creates one with the two files and generated notes.
- **A release without a ZIP** (notes written in advance, or a draft): it adds the two files
  and leaves the title and notes alone.
- **A release that already has a ZIP:** nothing is replaced or deleted, because the catalog
  at homebrew.page records each release ZIP's checksum and the store refuses a file that
  differs. The run ends green with a warning that those files were not published by it and
  may have no attestation.

A release ZIP built by the workflow can be checked with
`gh attestation verify PPSA99001.zip -R blackbearreloaded/ProsperoRadio` (GitHub CLI).
