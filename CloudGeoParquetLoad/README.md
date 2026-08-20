# CloudGeoParquetLoad

Probes a remote GeoParquet object with HTTP byte ranges, then opens it directly through GDAL `/vsicurl/` without downloading the complete file.

The URL is editable. The default is the official OGC GeoParquet example dataset.
