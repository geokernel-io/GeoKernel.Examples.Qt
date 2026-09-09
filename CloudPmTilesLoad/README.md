# CloudPmTilesLoad

Probes a remote PMTiles v3 archive with HTTP byte ranges, then opens it directly through GDAL `/vsicurl/` without downloading the complete archive.

The URL is editable. The default is the public Protomaps vector Firenze dataset.
