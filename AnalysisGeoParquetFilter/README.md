# AnalysisGeoParquetFilter

AnalysisGeoParquetFilter demonstrates the backend-neutral `GeoKernel.Analysis` workflow from request planning to Viewer-ready output.

## Workflow

- Downloads and extracts the existing `stockholm_data.zip` sample through the standard Qt sample-data helper.
- Creates an `AnalysisRequest` for a GeoParquet attribute and BBOX filter.
- Uses `AnalysisBackend::Auto`, allowing the planner to select the lowest-cost capable backend.
- Runs asynchronously and reports planning, execution, and materialization progress without blocking the UI.
- Converts the backend-neutral `AnalysisResult` into `GisLayerVector` with `AnalysisLayerMaterializer`.
- Displays the selected backend, execution plan, attempts, row counts, and warnings alongside the map.

This example deliberately contains no backend-specific query or WKB parsing code.
