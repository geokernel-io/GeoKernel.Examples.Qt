# DuckDbGeoParquetAnalytics

DuckDbGeoParquetAnalytics demonstrates measurable GeoParquet query gains from predicate, spatial bounding-box, and column-projection pushdown with DuckDB.

## Workflow

- Downloads and extracts the existing `stockholm_data.zip` sample through the standard Qt sample-data helper.
- Uses `stockholm_buildings.parquet` and warms the same DuckDB connection before measurement.
- Runs a full-column/full-row transfer followed by equivalent filtering in the application.
- Runs the optimized query with class, bounding-box, column projection, and limit pushed into DuckDB.
- Displays the optimized result in `GisViewer`.
- Reports elapsed time, scanned/transferred rows, transferred geometry bytes, reduction percentages, and speedup.

The comparison intentionally uses the same DuckDB engine and connection for both paths. This isolates the benefit of query pushdown from differences between unrelated file readers.
