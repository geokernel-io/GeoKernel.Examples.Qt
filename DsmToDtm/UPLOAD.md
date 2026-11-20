# SampleData upload handoff

Attach this exact file to the **existing `v1` release** of
`geokernel-io/GeoKernel.SampleData`:

`D:\projects\GeoKernel.Examples.Qt\DsmToDtm\outputs\sample-upload\zurich_dsm_to_dtm_v2.zip`

- ZIP bytes: **3,681,613** (about 3.51 MiB).
- SHA-256: `91962e0029c57738ed34d2bdef8d104ef751f7b988a582571faddf658a2b0331`
- Keep the filename **`zurich_dsm_to_dtm_v2.zip`** unchanged.
- Upload the ZIP itself, not the extracted files or a ZIP made by another tool.
- Uploading to the repository's regular file tree does not create the release URL.
- The neighboring `zurich_dsm_to_dtm_v2.zip.sha256` is provided for manual checking;
  the application already embeds this hash and all individual file hashes.

Release page:

https://github.com/geokernel-io/GeoKernel.SampleData/releases/tag/v1

Expected public asset URL:

https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/zurich_dsm_to_dtm_v2.zip

The source code already uses that address. Startup downloads/verifies/extracts it
and then reuses the verified local cache. The full file list, coordinate grid,
source attribution, model adaptation and score interpretation are in README.md
and inside the ZIP. Model weights are not part of this upload.

This is the corrected **v2** asset. It supersedes the earlier v1 candidate,
preserves EPSG:2056, and includes the PROJ coordinate database with its license.
Add it as a separate v2-named asset to the same GitHub v1 release.
