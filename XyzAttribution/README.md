# XyzAttribution

XyzAttribution demonstrates how to configure and display attribution text for an XYZ tile layer in GeoKernel.

## Overview

It creates an OpenStreetMap XYZ layer, stores attribution metadata on the layer, and displays the same attribution as an overlay in the map view. The toolbar lets the user apply the standard OpenStreetMap attribution or enter custom provider text at runtime, while the details panel shows the active metadata and relevant `GisLayerXYZ` API calls.

## GIS Workflow

This example shows how to:

- Create an XYZ layer from a URL template
- Configure attribution metadata on an XYZ layer
- Display attribution text as a map overlay
- Update provider attribution interactively
- Enable local caching for online tiles
- Inspect the active attribution and XYZ layer workflow
- Navigate an online Web Mercator map
- Preserve attribution metadata in map projects
